/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/api/RouterInterfaceApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

using namespace facebook::fboss;

class RouterInterfaceApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    routerInterfaceApi = std::make_unique<RouterInterfaceApi>();
  }
  RouterInterfaceSaiId createRouterInterface(
      sai_object_id_t vr = 42,
      sai_object_id_t vlan = 43) {
    SaiRouterInterfaceTraits::Attributes::Type typeAttribute(
        SAI_ROUTER_INTERFACE_TYPE_VLAN);
    SaiRouterInterfaceTraits::Attributes::VirtualRouterId
        virtualRouterIdAttribute(vr);
    SaiRouterInterfaceTraits::Attributes::VlanId vlanIdAttribute(vlan);
    auto rifId = routerInterfaceApi->create<SaiRouterInterfaceTraits>(
        {virtualRouterIdAttribute,
         typeAttribute,
         vlanIdAttribute,
         std::nullopt,
         std::nullopt},
        0);
    EXPECT_EQ(rifId, fs->rim.get(rifId).id);
    EXPECT_EQ(vr, fs->rim.get(rifId).virtualRouterId);
    EXPECT_EQ(vlan, fs->rim.get(rifId).vlanId);
    return rifId;
  }
  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<RouterInterfaceApi> routerInterfaceApi;
  sai_object_id_t switchId;
};

TEST_F(RouterInterfaceApiTest, create) {
  createRouterInterface();
}

TEST_F(RouterInterfaceApiTest, setSrcMac) {
  auto rifId = createRouterInterface();
  folly::MacAddress mac("42:42:42:42:42:42");
  SaiRouterInterfaceTraits::Attributes::SrcMac ma(mac);
  SaiRouterInterfaceTraits::Attributes::SrcMac ma2;
  EXPECT_NE(mac, routerInterfaceApi->getAttribute(rifId, ma2));
  routerInterfaceApi->setAttribute(rifId, ma);
  EXPECT_EQ(mac, routerInterfaceApi->getAttribute(rifId, ma2));
}

TEST_F(RouterInterfaceApiTest, setVrId) {
  auto rifId = createRouterInterface();
  SaiRouterInterfaceTraits::Attributes::VirtualRouterId
      virtualRouterIdAttribute(10);
  SaiRouterInterfaceTraits::Attributes::VirtualRouterId
      virtualRouterIdAttribute2;
  EXPECT_EQ(
      42, routerInterfaceApi->getAttribute(rifId, virtualRouterIdAttribute2));
  EXPECT_THROW(
      try {
        routerInterfaceApi->setAttribute(rifId, virtualRouterIdAttribute);
      } catch (const SaiApiError& e) {
        EXPECT_EQ(e.getSaiStatus(), SAI_STATUS_INVALID_PARAMETER);
        throw;
      },
      SaiApiError);
  EXPECT_EQ(
      42, routerInterfaceApi->getAttribute(rifId, virtualRouterIdAttribute2));
}

TEST_F(RouterInterfaceApiTest, setVlanId) {
  auto rifId = createRouterInterface();
  SaiRouterInterfaceTraits::Attributes::VlanId vlanIdAttribute(10);
  SaiRouterInterfaceTraits::Attributes::VlanId vlanIdAttribute2;
  EXPECT_EQ(43, routerInterfaceApi->getAttribute(rifId, vlanIdAttribute2));
  EXPECT_THROW(
      try {
        routerInterfaceApi->setAttribute(rifId, vlanIdAttribute);
      } catch (const SaiApiError& e) {
        EXPECT_EQ(e.getSaiStatus(), SAI_STATUS_INVALID_PARAMETER);
        throw;
      },
      SaiApiError);

  EXPECT_EQ(43, routerInterfaceApi->getAttribute(rifId, vlanIdAttribute2));
}

TEST_F(RouterInterfaceApiTest, setMtu) {
  auto rifId = createRouterInterface();
  sai_uint32_t mtu{9000};
  SaiRouterInterfaceTraits::Attributes::Mtu mtu1{mtu};
  SaiRouterInterfaceTraits::Attributes::Mtu mtu2;
  EXPECT_NE(mtu, routerInterfaceApi->getAttribute(rifId, mtu2));
  EXPECT_EQ(1514 /*default*/, routerInterfaceApi->getAttribute(rifId, mtu2));
  routerInterfaceApi->setAttribute(rifId, mtu1);
  EXPECT_EQ(mtu, routerInterfaceApi->getAttribute(rifId, mtu2));
}
