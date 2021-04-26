/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/platforms/sai/SaiPlatformPort.h"
#include "fboss/lib/fpga/FbDomFpga.h"

namespace facebook::fboss {

class SaiCloudRipperPlatformPort : public SaiPlatformPort {
 public:
  explicit SaiCloudRipperPlatformPort(PortID id, SaiPlatform* platform)
      : SaiPlatformPort(id, platform) {}
  virtual uint32_t getPhysicalLaneId(uint32_t chipId, uint32_t logicalLane)
      const override;
  virtual bool supportsTransceiver() const override;
  uint32_t getCurrentLedState() const override;

 private:
  FbDomFpga::LedColor getLedState(bool up, bool adminUp) const;
  FbDomFpga::LedColor currentLedState_;
};

} // namespace facebook::fboss
