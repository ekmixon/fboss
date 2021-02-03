/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmQosPolicy.h"

#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPlatform.h"
#include "fboss/agent/hw/bcm/BcmPortQueueManager.h"
#include "fboss/agent/hw/bcm/BcmQosMap.h"
#include "fboss/agent/hw/bcm/BcmQosUtils.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/BcmWarmBootCache.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/QosPolicy.h"

namespace {
constexpr int kDefaultProfileId = 0;
// array with index representing the traffic class and array value is priority
// group (PG) defaults from HW required for mapping incoming traffic to PG on
// ingress for PFC purposes
std::vector<int>
    kDefaultTrafficClassToPg{7, 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0};
} // unnamed namespace

namespace facebook::fboss {

BcmQosPolicy::BcmQosPolicy(
    BcmSwitch* hw,
    const std::shared_ptr<QosPolicy>& qosPolicy)
    : hw_(hw) {
  programIngressDscpQosMap(qosPolicy);
  programIngressExpQosMap(qosPolicy);
  programEgressExpQosMap(qosPolicy);
  programTrafficClassToPgMap(qosPolicy);
}

BcmQosPolicyHandle BcmQosPolicy::getHandle(BcmQosMap::Type type) const {
  switch (type) {
    case BcmQosMap::Type::IP_INGRESS:
      if (ingressDscpQosMap_) {
        return static_cast<BcmQosPolicyHandle>(ingressDscpQosMap_->getHandle());
      }
      break;
    case BcmQosMap::Type::IP_EGRESS:
      return static_cast<BcmQosPolicyHandle>(-1);
      break;
    case BcmQosMap::Type::MPLS_INGRESS:
      if (ingressExpQosMap_) {
        return static_cast<BcmQosPolicyHandle>(ingressExpQosMap_->getHandle());
      }

      break;
    case BcmQosMap::Type::MPLS_EGRESS:
      if (egressExpQosMap_) {
        return static_cast<BcmQosPolicyHandle>(egressExpQosMap_->getHandle());
      }
      break;
  }
  return static_cast<BcmQosPolicyHandle>(-1);
}

void BcmQosPolicy::update(
    const std::shared_ptr<QosPolicy>& oldQosPolicy,
    const std::shared_ptr<QosPolicy>& newQosPolicy) {
  CHECK(oldQosPolicy->getID() == newQosPolicy->getID());
  updateIngressDscpQosMap(oldQosPolicy, newQosPolicy);
  updateEgressExpQosMap(oldQosPolicy, newQosPolicy);
  updateIngressExpQosMap(oldQosPolicy, newQosPolicy);
  updateTrafficClassToPgMap(oldQosPolicy, newQosPolicy);
}

void BcmQosPolicy::remove() {
  programTrafficClassToPg(getBcmDefaultTrafficClassToPgArr());
}

void BcmQosPolicy::updateIngressDscpQosMap(
    const std::shared_ptr<QosPolicy>& oldQosPolicy,
    const std::shared_ptr<QosPolicy>& newQosPolicy) {
  if (!ingressDscpQosMap_) {
    programIngressDscpQosMap(newQosPolicy);
    return;
  }
  const auto& oldRules = oldQosPolicy->getDscpMap().from();
  const auto& newRules = newQosPolicy->getDscpMap().from();

  std::vector<TrafficClassToQosAttributeMapEntry<DSCP>> toBeRemoved;
  std::set_difference(
      oldRules.begin(),
      oldRules.end(),
      newRules.begin(),
      newRules.end(),
      std::inserter(toBeRemoved, toBeRemoved.end()));
  for (const auto& qosRule : toBeRemoved) {
    ingressDscpQosMap_->removeRule(
        BcmPortQueueManager::CosQToBcmInternalPriority(qosRule.trafficClass()),
        qosRule.attr());
  }

  std::vector<TrafficClassToQosAttributeMapEntry<DSCP>> toBeAdded;
  std::set_difference(
      newRules.begin(),
      newRules.end(),
      oldRules.begin(),
      oldRules.end(),
      std::inserter(toBeAdded, toBeAdded.end()));
  for (const auto& qosRule : toBeAdded) {
    ingressDscpQosMap_->addRule(
        BcmPortQueueManager::CosQToBcmInternalPriority(qosRule.trafficClass()),
        qosRule.attr());
  }
}

void BcmQosPolicy::programPfcPriorityToPg(std::vector<int>& pfcPriorityPg) {
  if (!hw_->getPlatform()->getAsic()->isSupported(HwAsic::Feature::PFC)) {
    return;
  }
  // an array with index representing pfc priority
  // value is PG Id
  auto rv = bcm_cosq_priority_group_pfc_priority_mapping_profile_set(
      hw_->getUnit(),
      kDefaultProfileId,
      pfcPriorityPg.size(),
      pfcPriorityPg.data());
  bcmCheckError(
      rv,
      "Failed to program bcm_cosq_priority_group_pfc_priority_mapping_profile_set, size: ",
      pfcPriorityPg.size());
}

void BcmQosPolicy::programTrafficClassToPg(
    const std::vector<int>& trafficClassPg) {
  if (!hw_->getPlatform()->getAsic()->isSupported(HwAsic::Feature::PFC)) {
    return;
  }
  // program unicast mapping
  programPriorityGroupMapping(
      bcmCosqInputPriPriorityGroupUcMapping,
      trafficClassPg,
      "bcmCosqInputPriPriorityGroupUcMapping");

  // program mcast mapping
  programPriorityGroupMapping(
      bcmCosqInputPriPriorityGroupMcMapping,
      trafficClassPg,
      "bcmCosqInputPriPriorityGroupMcMapping");
}

// case 1: !old cfg, !new cfg
// case 2: !old cfg, new cfg
// case 3: old cfg, !new cfg
// case 4: old cfg!= new cfg
void BcmQosPolicy::updatePfcPriorityToPgMap(
    const std::shared_ptr<QosPolicy>& oldQosPolicy,
    const std::shared_ptr<QosPolicy>& newQosPolicy) {
  if (!newQosPolicy->getPfcPriorityToPgId() &&
      !oldQosPolicy->getPfcPriorityToPgId()) {
    // case 1
    // nothing to do with pfc priority <-> pg id mapping
    return;
  }
  // all other cases handled here
  programPfcPriorityToPgMap(newQosPolicy);
}

// case 1: !old cfg, !new cfg
// case 2: !old cfg, new cfg
// case 3: old cfg, !new cfg
// case 4: old cfg!= new cfg
void BcmQosPolicy::updateTrafficClassToPgMap(
    const std::shared_ptr<QosPolicy>& oldQosPolicy,
    const std::shared_ptr<QosPolicy>& newQosPolicy) {
  if (!newQosPolicy->getTrafficClassToPgId() &&
      !oldQosPolicy->getTrafficClassToPgId()) {
    // case 1
    // nothing to do with traffic class <-> pg id mapping
    return;
  }
  // all other cases handled here
  programTrafficClassToPgMap(newQosPolicy);
}

void BcmQosPolicy::updateIngressExpQosMap(
    const std::shared_ptr<QosPolicy>& oldQosPolicy,
    const std::shared_ptr<QosPolicy>& newQosPolicy) {
  CHECK(oldQosPolicy->getID() == newQosPolicy->getID());
  if (!ingressExpQosMap_) {
    programIngressExpQosMap(newQosPolicy);
    return;
  }
  const auto& oldRules = oldQosPolicy->getExpMap().from();
  const auto& newRules = newQosPolicy->getExpMap().from();

  std::vector<TrafficClassToQosAttributeMapEntry<EXP>> toBeRemoved;
  std::set_difference(
      oldRules.begin(),
      oldRules.end(),
      newRules.begin(),
      newRules.end(),
      std::inserter(toBeRemoved, toBeRemoved.end()));
  for (const auto& qosRule : toBeRemoved) {
    ingressExpQosMap_->removeRule(
        BcmPortQueueManager::CosQToBcmInternalPriority(qosRule.trafficClass()),
        qosRule.attr());
  }

  std::vector<TrafficClassToQosAttributeMapEntry<EXP>> toBeAdded;
  std::set_difference(
      newRules.begin(),
      newRules.end(),
      oldRules.begin(),
      oldRules.end(),
      std::inserter(toBeAdded, toBeAdded.end()));
  for (const auto& qosRule : toBeAdded) {
    ingressExpQosMap_->addRule(
        BcmPortQueueManager::CosQToBcmInternalPriority(qosRule.trafficClass()),
        qosRule.attr());
  }
}

void BcmQosPolicy::updateEgressExpQosMap(
    const std::shared_ptr<QosPolicy>& oldQosPolicy,
    const std::shared_ptr<QosPolicy>& newQosPolicy) {
  CHECK(oldQosPolicy->getID() == newQosPolicy->getID());
  if (!egressExpQosMap_) {
    programEgressExpQosMap(newQosPolicy);
    return;
  }
  const auto& oldRules = oldQosPolicy->getExpMap().to();
  const auto& newRules = newQosPolicy->getExpMap().to();

  std::vector<TrafficClassToQosAttributeMapEntry<EXP>> toBeRemoved;
  std::set_difference(
      oldRules.begin(),
      oldRules.end(),
      newRules.begin(),
      newRules.end(),
      std::inserter(toBeRemoved, toBeRemoved.end()));
  for (const auto& qosRule : toBeRemoved) {
    egressExpQosMap_->removeRule(
        BcmPortQueueManager::CosQToBcmInternalPriority(qosRule.trafficClass()),
        qosRule.attr());
  }

  std::vector<TrafficClassToQosAttributeMapEntry<EXP>> toBeAdded;
  std::set_difference(
      newRules.begin(),
      newRules.end(),
      oldRules.begin(),
      oldRules.end(),
      std::inserter(toBeAdded, toBeAdded.end()));
  for (const auto& qosRule : toBeAdded) {
    egressExpQosMap_->addRule(
        BcmPortQueueManager::CosQToBcmInternalPriority(qosRule.trafficClass()),
        qosRule.attr());
  }
}

bool BcmQosPolicy::policyMatches(
    const std::shared_ptr<QosPolicy>& qosPolicy) const {
  if (ingressDscpQosMap_->size() != qosPolicy->getDscpMap().from().size()) {
    return false;
  }
  for (const auto& rule : qosPolicy->getDscpMap().from()) {
    if (!ingressDscpQosMap_->ruleExists(
            BcmPortQueueManager::CosQToBcmInternalPriority(rule.trafficClass()),
            rule.attr())) {
      return false;
    }
  }
  return true;
}

void BcmQosPolicy::programIngressDscpQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  if (qosPolicy->getDscpMap().from().empty()) {
    return;
  }
  auto warmBootCache = hw_->getWarmBootCache();
  auto qosMapItr =
      warmBootCache->findQosMap(qosPolicy, BcmQosMap::Type::IP_INGRESS);

  if (qosMapItr != warmBootCache->QosMapId2QosMapEnd()) {
    ingressDscpQosMap_ = std::move(qosMapItr->second);
    warmBootCache->programmed(
        qosPolicy->getName(), BcmQosMap::Type::IP_INGRESS, qosMapItr);
    return;
  }

  ingressDscpQosMap_ =
      std::make_unique<BcmQosMap>(hw_, BcmQosMap::Type::IP_INGRESS);
  for (const auto& dscpToTrafficClass : qosPolicy->getDscpMap().from()) {
    ingressDscpQosMap_->addRule(
        BcmPortQueueManager::CosQToBcmInternalPriority(
            dscpToTrafficClass.trafficClass()),
        dscpToTrafficClass.attr());
  }
}

void BcmQosPolicy::programIngressExpQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  if (qosPolicy->getExpMap().from().empty()) {
    return;
  }
  auto warmBootCache = hw_->getWarmBootCache();
  auto qosMapItr =
      warmBootCache->findQosMap(qosPolicy, BcmQosMap::Type::MPLS_INGRESS);

  if (qosMapItr != warmBootCache->QosMapId2QosMapEnd()) {
    ingressExpQosMap_ = std::move(qosMapItr->second);
    warmBootCache->programmed(
        qosPolicy->getName(), BcmQosMap::Type::MPLS_INGRESS, qosMapItr);
    return;
  }
  ingressExpQosMap_ =
      std::make_unique<BcmQosMap>(hw_, BcmQosMap::Type::MPLS_INGRESS);
  for (const auto& expToTrafficClass : qosPolicy->getExpMap().from()) {
    ingressExpQosMap_->addRule(
        BcmPortQueueManager::CosQToBcmInternalPriority(
            expToTrafficClass.trafficClass()),
        expToTrafficClass.attr());
  }
}

void BcmQosPolicy::programEgressExpQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  if (qosPolicy->getExpMap().to().empty()) {
    return;
  }
  auto warmBootCache = hw_->getWarmBootCache();
  auto qosMapItr =
      warmBootCache->findQosMap(qosPolicy, BcmQosMap::Type::MPLS_EGRESS);

  if (qosMapItr != warmBootCache->QosMapId2QosMapEnd()) {
    egressExpQosMap_ = std::move(qosMapItr->second);
    warmBootCache->programmed(
        qosPolicy->getName(), BcmQosMap::Type::MPLS_EGRESS, qosMapItr);
    return;
  }
  egressExpQosMap_ =
      std::make_unique<BcmQosMap>(hw_, BcmQosMap::Type::MPLS_EGRESS);
  for (const auto& trafficClassToExp : qosPolicy->getExpMap().to()) {
    egressExpQosMap_->addRule(
        BcmPortQueueManager::CosQToBcmInternalPriority(
            trafficClassToExp.trafficClass()),
        trafficClassToExp.attr());
  }
}

const BcmQosMap* BcmQosPolicy::getIngressDscpQosMap() const {
  return ingressDscpQosMap_.get();
}

const BcmQosMap* BcmQosPolicy::getIngressExpQosMap() const {
  return ingressExpQosMap_.get();
}

const BcmQosMap* BcmQosPolicy::getEgressExpQosMap() const {
  return egressExpQosMap_.get();
}

void BcmQosPolicy::programPriorityGroupMapping(
    const bcm_cosq_priority_group_mapping_profile_type_t profileType,
    const std::vector<int>& trafficClassToPgId,
    const std::string& profileTypeStr) {
  auto tmpTrafficClassToPgId =
      const_cast<std::vector<int>&>(trafficClassToPgId);
  auto rv = bcm_cosq_priority_group_mapping_profile_set(
      hw_->getUnit(),
      kDefaultProfileId,
      profileType,
      trafficClassToPgId.size(),
      tmpTrafficClassToPgId.data());
  bcmCheckError(
      rv,
      "failed to program ",
      profileTypeStr,
      " size: ",
      trafficClassToPgId.size(),
      " type: ",
      profileType);
}

void BcmQosPolicy::programPfcPriorityToPgMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  std::vector<int> pfcPriorityToPg = getBcmDefaultPfcPriorityToPgArr();
  if (auto pfcPriorityToPgMap = qosPolicy->getPfcPriorityToPgId()) {
    // override with what user configures
    for (const auto& entry : *pfcPriorityToPgMap) {
      CHECK_LE(
          static_cast<int>(entry.first),
          cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX())
          << "Policy: " << qosPolicy->getName() << " has invalid pfc priority"
          << (int)entry.first;
      CHECK_LE(entry.second, cfg::switch_config_constants::PORT_PG_VALUE_MAX())
          << " Policy: " << qosPolicy->getName() << " has invalid PG ID "
          << entry.second;
      CHECK_GT(pfcPriorityToPg.size(), static_cast<int>(entry.first))
          << " Policy: " << qosPolicy->getName()
          << " has invalid pfcPriorityToPg size " << pfcPriorityToPg.size();
      pfcPriorityToPg[entry.first] = entry.second;
    }
  }
  programPfcPriorityToPg(pfcPriorityToPg);
}

void BcmQosPolicy::programTrafficClassToPgMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  // init the array with HW defaults
  std::vector<int> trafficClassToPg = getBcmDefaultTrafficClassToPgArr();
  if (auto trafficClassToPgMap = qosPolicy->getTrafficClassToPgId()) {
    // override with what user configures
    for (const auto& entry : *trafficClassToPgMap) {
      CHECK_LE(static_cast<int>(entry.first), BCM_PRIO_MAX)
          << "Policy: " << qosPolicy->getName() << " has invalid priority "
          << (int)entry.first;
      CHECK_LE(entry.second, cfg::switch_config_constants::PORT_PG_VALUE_MAX())
          << " Policy: " << qosPolicy->getName() << " has invalid PG ID "
          << entry.second;
      CHECK_GT(trafficClassToPg.size(), static_cast<int>(entry.first))
          << " Policy: " << qosPolicy->getName()
          << " has invalid trafficClassToPg size " << trafficClassToPg.size();
      trafficClassToPg[entry.first] = entry.second;
    }
  }
  programTrafficClassToPg(trafficClassToPg);
}
} // namespace facebook::fboss
