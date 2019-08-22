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

#include <memory>

namespace facebook {
namespace fboss {

class AgentConfig;
class Platform;
class SaiPlatform;
class SaiProductInfo;

std::unique_ptr<SaiPlatform> createSaiPlatform();
std::unique_ptr<Platform> initSaiPlatform(
    std::unique_ptr<AgentConfig> config = nullptr);
} // namespace fboss
} // namespace facebook
