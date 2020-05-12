/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/LookupClassRouteUpdater.h"

#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

// Helper methods

void LookupClassRouteUpdater::reAddAllRoutes(const StateDelta& stateDelta) {
  auto& newState = stateDelta.newState();

  for (const auto& routeTable : *newState->getRouteTables()) {
    auto rid = routeTable->getID();

    for (const auto& route : *(routeTable->getRibV6()->routes())) {
      processRouteAdded(stateDelta, rid, route);
    }

    for (const auto& route : *(routeTable->getRibV4()->routes())) {
      processRouteAdded(stateDelta, rid, route);
    }
  }
}

bool LookupClassRouteUpdater::vlanHasOtherPortsWithClassIDs(
    const std::shared_ptr<SwitchState>& switchState,
    const std::shared_ptr<Vlan>& vlan,
    const std::shared_ptr<Port>& removedPort) {
  for (auto& [portID, portInfo] : vlan->getPorts()) {
    auto port = switchState->getPorts()->getPortIf(portID);

    if (portID != removedPort->getID() &&
        port->getLookupClassesToDistributeTrafficOn().size() != 0) {
      return true;
    }
  }

  return false;
}

void LookupClassRouteUpdater::removeNextHopsForSubnet(
    const StateDelta& stateDelta,
    const folly::CIDRNetwork& subnet,
    const std::shared_ptr<Vlan>& vlan) {
  auto& [ipAddress, mask] = subnet;

  auto it = nextHopAndVlanToPrefixes_.begin();
  while (it != nextHopAndVlanToPrefixes_.end()) {
    const auto& [nextHop, vlanID] = it->first;

    // Element pointed by the iterator would be deleted from
    // nextHopAndVlanToPrefixes_ as part of processNeighborRemoved processing.
    // Thus, advance the iterator to next element.
    ++it;

    if (vlanID == vlan->getID() && nextHop.inSubnet(ipAddress, mask)) {
      if (nextHop.isV6()) {
        auto ndpEntry = vlan->getNdpTable()->getEntryIf(nextHop.asV6());
        if (ndpEntry) {
          processNeighborRemoved(stateDelta, vlan->getID(), ndpEntry);
        }
      } else if (nextHop.isV4()) {
        auto arpEntry = vlan->getArpTable()->getEntryIf(nextHop.asV4());
        if (arpEntry) {
          processNeighborRemoved(stateDelta, vlan->getID(), arpEntry);
        }
      }
    }
  }
}

std::optional<cfg::AclLookupClass>
LookupClassRouteUpdater::getClassIDForNeighbor(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlanID,
    const folly::IPAddress& ipAddress) {
  auto vlan = switchState->getVlans()->getVlanIf(vlanID);
  if (!vlan) {
    return std::nullopt;
  }

  if (ipAddress.isV6()) {
    auto ndpEntry = vlan->getNdpTable()->getEntryIf(ipAddress.asV6());
    return ndpEntry ? ndpEntry->getClassID() : std::nullopt;
  } else if (ipAddress.isV4()) {
    auto arpEntry = vlan->getArpTable()->getEntryIf(ipAddress.asV4());
    return arpEntry ? arpEntry->getClassID() : std::nullopt;
  }

  return std::nullopt;
}

// Methods for dealing with vlan2SubnetsCache_

bool LookupClassRouteUpdater::belongsToSubnetInCache(
    VlanID vlanID,
    const folly::IPAddress& ipToSearch) {
  auto it = vlan2SubnetsCache_.find(vlanID);
  if (it != vlan2SubnetsCache_.end()) {
    auto subnetsCache = it->second;
    for (const auto& [ipAddress, mask] : subnetsCache) {
      if (ipToSearch.inSubnet(ipAddress, mask)) {
        return true;
      }
    }
  }

  return false;
}

void LookupClassRouteUpdater::updateSubnetsCache(
    const StateDelta& stateDelta,
    std::shared_ptr<Port> port,
    bool reAddAllRoutesEnabled) {
  auto& newState = stateDelta.newState();

  for (const auto& [vlanID, vlanInfo] : port->getVlans()) {
    auto vlan = newState->getVlans()->getVlanIf(vlanID);
    if (!vlan) {
      continue;
    }

    auto& subnetsCache = vlan2SubnetsCache_[vlanID];
    auto interface =
        newState->getInterfaces()->getInterfaceIf(vlan->getInterfaceID());
    if (interface) {
      for (auto address : interface->getAddresses()) {
        subnetsCache.insert(address);

        if (reAddAllRoutesEnabled) {
          /*
           * When a new subnet is added to the cache, the nextHops of existing
           * routes may become eligible for caching in
           * nextHopAndVlanToPrefixes_. Furthermore, such a nextHop may have
           * classID associated with it, and in that case, the corresponding
           * route could inherit that classID. Thus, re-add all the routes.
           */
          reAddAllRoutes(stateDelta);
        }
      }
    }
  }
}

// Methods for handling port updates

void LookupClassRouteUpdater::processPortAdded(
    const StateDelta& stateDelta,
    const std::shared_ptr<Port>& addedPort,
    bool reAddAllRoutesEnabled) {
  CHECK(addedPort);

  if (addedPort->getLookupClassesToDistributeTrafficOn().size() == 0) {
    /*
     *  Only downlink ports connecting to MH-NIC have lookupClasses
     *  configured. For all the other ports, no-op.
     */
    return;
  }

  updateSubnetsCache(stateDelta, addedPort, reAddAllRoutesEnabled);
}

void LookupClassRouteUpdater::processPortRemoved(
    const StateDelta& stateDelta,
    const std::shared_ptr<Port>& removedPort) {
  CHECK(removedPort);

  if (removedPort->getLookupClassesToDistributeTrafficOn().size() == 0) {
    /*
     *  Only downlink ports connecting to MH-NIC have lookupClasses
     *  configured. For all the other ports, no-op.
     */
    return;
  }

  auto& newState = stateDelta.newState();

  for (const auto& [vlanID, vlanInfo] : removedPort->getVlans()) {
    auto vlanIter = vlan2SubnetsCache_.find(vlanID);
    if (vlanIter == vlan2SubnetsCache_.end()) {
      continue;
    }

    auto vlan = newState->getVlans()->getVlanIf(vlanID);
    if (!vlan || vlanHasOtherPortsWithClassIDs(newState, vlan, removedPort)) {
      continue;
    }

    auto interface =
        newState->getInterfaces()->getInterfaceIf(vlan->getInterfaceID());
    if (!interface) {
      continue;
    }

    auto& subnetsCache = vlan2SubnetsCache_[vlanID];
    for (auto address : interface->getAddresses()) {
      subnetsCache.erase(address);
      removeNextHopsForSubnet(stateDelta, address, vlan);
    }
  }
}

void LookupClassRouteUpdater::processPortChanged(
    const StateDelta& stateDelta,
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  CHECK(oldPort && newPort);
  CHECK_EQ(oldPort->getID(), newPort->getID());

  if (oldPort->getLookupClassesToDistributeTrafficOn().size() == 0 &&
      newPort->getLookupClassesToDistributeTrafficOn().size() != 0) {
    // enable queue-per-host for this port
    processPortAdded(stateDelta, newPort, true /* re-all all routes */);
  } else if (
      oldPort->getLookupClassesToDistributeTrafficOn().size() != 0 &&
      newPort->getLookupClassesToDistributeTrafficOn().size() == 0) {
    // disable queue-per-host for this port
    processPortRemoved(stateDelta, oldPort);
  } else if (
      oldPort->getLookupClassesToDistributeTrafficOn().size() != 0 &&
      newPort->getLookupClassesToDistributeTrafficOn().size() != 0) {
    // queue-per-host remains enabled, but port's VLAN membership changed, readd
    if (oldPort->getVlans() != newPort->getVlans()) {
      processPortRemoved(stateDelta, oldPort);
      processPortAdded(stateDelta, newPort, true /* re-all all routes */);
    }
  }
}

void LookupClassRouteUpdater::processPortUpdates(const StateDelta& stateDelta) {
  for (const auto& delta : stateDelta.getPortsDelta()) {
    auto oldPort = delta.getOld();
    auto newPort = delta.getNew();

    if (!oldPort && newPort) {
      // processRouteUpdates is invoked after processPortAdd,
      // thus, we don't need to re-add all the routes.
      processPortAdded(
          stateDelta, newPort, false /* don't re-add all routes */);
    } else if (oldPort && !newPort) {
      processPortRemoved(stateDelta, oldPort);
    } else {
      processPortChanged(stateDelta, oldPort, newPort);
    }
  }
}

// Methods for handling neighbor updates

template <typename AddedNeighborT>
void LookupClassRouteUpdater::processNeighborAdded(
    const StateDelta& /*stateDelta*/,
    VlanID /*vlan*/,
    const std::shared_ptr<AddedNeighborT>& /*addedNeighbor*/) {}

template <typename RemovedNeighborT>
void LookupClassRouteUpdater::processNeighborRemoved(
    const StateDelta& /*stateDelta*/,
    VlanID /*vlan*/,
    const std::shared_ptr<RemovedNeighborT>& /*removedNeighbor*/) {}

template <typename ChangedNeighborT>
void LookupClassRouteUpdater::processNeighborChanged(
    const StateDelta& /*stateDelta*/,
    VlanID /*vlan*/,
    const std::shared_ptr<ChangedNeighborT>& /*oldNeighbor*/,
    const std::shared_ptr<ChangedNeighborT>& /*newNeighbor*/) {}

template <typename AddrT>
void LookupClassRouteUpdater::processNeighborUpdates(
    const StateDelta& stateDelta) {
  {
    using NeighborTableT = std::conditional_t<
        std::is_same<AddrT, folly::IPAddressV4>::value,
        ArpTable,
        NdpTable>;

    for (const auto& vlanDelta : stateDelta.getVlansDelta()) {
      auto newVlan = vlanDelta.getNew();
      if (!newVlan) {
        auto oldVlan = vlanDelta.getOld();
        for (const auto& oldNeighbor :
             *(oldVlan->template getNeighborTable<NeighborTableT>())) {
          processNeighborRemoved(stateDelta, oldVlan->getID(), oldNeighbor);
        }
        continue;
      }

      auto vlan = newVlan->getID();

      for (const auto& delta :
           vlanDelta.template getNeighborDelta<NeighborTableT>()) {
        auto oldNeighbor = delta.getOld();
        auto newNeighbor = delta.getNew();

        /*
         * At this point in time, queue-per-host fix is needed (and thus
         * supported) for physical link only.
         */
        if ((oldNeighbor && !oldNeighbor->getPort().isPhysicalPort()) ||
            (newNeighbor && !newNeighbor->getPort().isPhysicalPort())) {
          continue;
        }

        if (!oldNeighbor) {
          processNeighborAdded(stateDelta, vlan, newNeighbor);
        } else if (!newNeighbor) {
          processNeighborRemoved(stateDelta, vlan, oldNeighbor);
        } else {
          processNeighborChanged(stateDelta, vlan, oldNeighbor, newNeighbor);
        }
      }
    }
  }
}

// Methods for handling route updates

template <typename RouteT>
std::optional<cfg::AclLookupClass>
LookupClassRouteUpdater::addRouteAndFindClassID(
    const StateDelta& stateDelta,
    RouterID rid,
    const std::shared_ptr<RouteT>& addedRoute) {
  auto ridAndCidr = std::make_pair(
      rid,
      folly::CIDRNetwork{addedRoute->prefix().network,
                         addedRoute->prefix().mask});

  auto& newState = stateDelta.newState();
  std::optional<cfg::AclLookupClass> routeClassID{std::nullopt};
  for (const auto& nextHop : addedRoute->getForwardInfo().getNextHopSet()) {
    auto vlanID =
        newState->getInterfaces()->getInterfaceIf(nextHop.intf())->getVlanID();
    if (!belongsToSubnetInCache(vlanID, nextHop.addr())) {
      continue;
    }

    auto neighborClassID =
        getClassIDForNeighbor(newState, vlanID, nextHop.addr());

    /*
     * The nextHopAndVlan may already be cached if:
     *   - it is also nextHop for some other route that was previously added.
     *   - during processNeighborAdded
     * retrieve previously cached entry, and if absent, create new entry.
     */
    auto& [withClassIDPrefixes, withoutClassIDPrefixes] =
        nextHopAndVlanToPrefixes_[std::make_pair(nextHop.addr(), vlanID)];

    /*
     * In the current implementation, route inherits classID of the 'first'
     * nexthop that has classID. This could be revised in future if necessary.
     */
    if (!routeClassID.has_value() && neighborClassID.has_value()) {
      routeClassID = neighborClassID;
      withClassIDPrefixes.insert(ridAndCidr);
    } else {
      withoutClassIDPrefixes.insert(ridAndCidr);
    }
  }

  if (routeClassID.has_value()) {
    auto [it, inserted] = allPrefixesWithClassID_.insert(ridAndCidr);
    CHECK(inserted);
  }

  return routeClassID;
}

template <typename RouteT>
void LookupClassRouteUpdater::processRouteAdded(
    const StateDelta& stateDelta,
    RouterID rid,
    const std::shared_ptr<RouteT>& addedRoute) {
  CHECK(addedRoute);

  /*
   * Non-resolved routes are not programmed in HW
   * routes to CPU have no nextHops, and can't get classID.
   */
  if (!addedRoute->isResolved() || addedRoute->isToCPU()) {
    return;
  }

  auto ridAndCidr = std::make_pair(
      rid,
      folly::CIDRNetwork{addedRoute->prefix().network,
                         addedRoute->prefix().mask});
  auto routeClassID = addRouteAndFindClassID(stateDelta, rid, addedRoute);

  if (routeClassID.has_value()) {
    updateClassIDsForRoutes({std::make_pair(ridAndCidr, routeClassID)});
  }
}

template <typename RouteT>
void LookupClassRouteUpdater::processRouteRemoved(
    const StateDelta& stateDelta,
    RouterID rid,
    const std::shared_ptr<RouteT>& removedRoute) {
  CHECK(removedRoute);

  /*
   * Non-resolved routes are not programmed in HW
   * routes to CPU (have no nextHops), and can't get classID.
   */
  if (!removedRoute->isResolved() || removedRoute->isToCPU()) {
    return;
  }

  // ClassID is associated with (and refCnt'ed for) MAC and ARP/NDP neighbor.
  // Route simply inherits classID of its nexthop, so we need not release
  // classID here. Furthermore, the route is already removed, so we don't need
  // to schedule a state update either. Just remove the route from local data
  // structures.

  auto ridAndCidr = std::make_pair(
      rid,
      folly::CIDRNetwork{removedRoute->prefix().network,
                         removedRoute->prefix().mask});

  auto routeClassID = removedRoute->getClassID();
  auto& newState = stateDelta.newState();
  for (const auto& nextHop : removedRoute->getForwardInfo().getNextHopSet()) {
    auto vlanID =
        newState->getInterfaces()->getInterfaceIf(nextHop.intf())->getVlanID();
    if (!belongsToSubnetInCache(vlanID, nextHop.addr())) {
      continue;
    }

    auto it =
        nextHopAndVlanToPrefixes_.find(std::make_pair(nextHop.addr(), vlanID));
    CHECK(it != nextHopAndVlanToPrefixes_.end());
    auto& [withClassIDPrefixes, withoutClassIDPrefixes] = it->second;

    // The prefix has to be in either of the sets.
    auto numErased = withClassIDPrefixes.erase(ridAndCidr) +
        withoutClassIDPrefixes.erase(ridAndCidr);
    CHECK_EQ(numErased, 1);

    if (withClassIDPrefixes.empty() && withoutClassIDPrefixes.empty()) {
      // if this was the only route this entry was NextHop for, and there is no
      // neighbor corresponding to this NextHop, erase it.
      auto vlan = newState->getVlans()->getVlanIf(vlanID);
      if (vlan) {
        if (nextHop.addr().isV6()) {
          auto ndpEntry =
              vlan->getNdpTable()->getEntryIf(nextHop.addr().asV6());
          if (!ndpEntry) {
            nextHopAndVlanToPrefixes_.erase(it);
          }
        } else if (nextHop.addr().isV4()) {
          auto arpEntry =
              vlan->getArpTable()->getEntryIf(nextHop.addr().asV4());
          if (!arpEntry) {
            nextHopAndVlanToPrefixes_.erase(it);
          }
        }
      }
    }
  }

  if (routeClassID.has_value()) {
    auto numErasedFromAllPrefixes = allPrefixesWithClassID_.erase(ridAndCidr);
    CHECK_EQ(numErasedFromAllPrefixes, 1);
  }
}

template <typename RouteT>
void LookupClassRouteUpdater::processRouteChanged(
    const StateDelta& stateDelta,
    RouterID rid,
    const std::shared_ptr<RouteT>& oldRoute,
    const std::shared_ptr<RouteT>& newRoute) {
  CHECK(oldRoute);
  CHECK(newRoute);

  if (!oldRoute->isResolved() && !newRoute->isResolved()) {
    return;
  } else if (!oldRoute->isResolved() && newRoute->isResolved()) {
    processRouteAdded(stateDelta, rid, newRoute);
  } else if (oldRoute->isResolved() && !newRoute->isResolved()) {
    processRouteRemoved(stateDelta, rid, oldRoute);
  } else if (oldRoute->isResolved() && newRoute->isResolved()) {
    /*
     * If the list of nexthops changes, a route may lose the nexthop it
     * inherited classID from. In that case, we need to find another reachable
     * nexthop for the route.
     *
     * This could be implemented by std::set_difference of getNextHopSet().
     * However, it is easier to remove the route and add it again.
     * processRouteRemoved does not schedule state update, so the only
     * additional overhead of this approach is some local computation.
     */
    if (oldRoute->getForwardInfo().getNextHopSet() !=
        newRoute->getForwardInfo().getNextHopSet()) {
      processRouteRemoved(stateDelta, rid, oldRoute);
      processRouteAdded(stateDelta, rid, newRoute);
    }
  }
}

template <typename AddrT>
void LookupClassRouteUpdater::processRouteUpdates(
    const StateDelta& stateDelta) {
  for (auto const& routeTableDelta : stateDelta.getRouteTablesDelta()) {
    auto const& newRouteTable = routeTableDelta.getNew();
    if (!newRouteTable) {
      auto const& oldRouteTable = routeTableDelta.getOld();
      for (const auto& oldRoute :
           *(oldRouteTable->template getRib<AddrT>()->routes())) {
        processRouteRemoved(stateDelta, oldRouteTable->getID(), oldRoute);
      }
      continue;
    }

    auto rid = newRouteTable->getID();
    for (auto const& routeDelta : routeTableDelta.getRoutesDelta<AddrT>()) {
      auto const& oldRoute = routeDelta.getOld();
      auto const& newRoute = routeDelta.getNew();

      if (!oldRoute) {
        processRouteAdded(stateDelta, rid, newRoute);
      } else if (!newRoute) {
        processRouteRemoved(stateDelta, rid, oldRoute);
      } else {
        processRouteChanged(stateDelta, rid, oldRoute, newRoute);
      }
    }
  }
}

// Methods for scheduling state updates

void LookupClassRouteUpdater::updateClassIDsForRoutes(
    const std::vector<RouteAndClassID>& /* routesAndClassIDs*/) {}

void LookupClassRouteUpdater::stateUpdated(const StateDelta& stateDelta) {
  /*
   * If vlan2SubnetsCache_ is updated after routes are added, every update to
   * vlan2SubnetsCache_ must check if the nextHops of previously processed
   * routes now become eligible for addition to nextHopAndVlanToPrefixes_.
   * This would require processing ALL the routes from the switchState, which
   * is expensive. We avoid that by processing port additions before processing
   * route additions (i.e. by calling processPortUpdates before
   * processRouteUpdates).
   */
  processPortUpdates(stateDelta);

  /*
   * Only RSWs connected to MH-NIC (e.g. Yosemite) need queue-per-host fix, and
   * thus have non-empty vlan2SubnetsCache_ (populated by processPortUpdates).
   * Skip the processing on other setups.
   */
  if (vlan2SubnetsCache_.empty()) {
    return;
  }

  processNeighborUpdates<folly::IPAddressV6>(stateDelta);
  processNeighborUpdates<folly::IPAddressV4>(stateDelta);

  processRouteUpdates<folly::IPAddressV6>(stateDelta);
  processRouteUpdates<folly::IPAddressV4>(stateDelta);
}

} // namespace facebook::fboss
