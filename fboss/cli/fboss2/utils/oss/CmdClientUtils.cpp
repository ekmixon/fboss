/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"
#include "fboss/mka_service/if/gen-cpp2/MKAService.h"

namespace facebook::fboss::utils {

std::unique_ptr<facebook::fboss::FbossCtrlAsyncClient> createAgentClient(
    const HostInfo& hostInfo) {
  auto agentPort = CmdGlobalOptions::getInstance()->getAgentThriftPort();
  return createPlaintextClient<facebook::fboss::FbossCtrlAsyncClient>(
      hostInfo, agentPort);
}

std::unique_ptr<facebook::fboss::QsfpServiceAsyncClient> createQsfpClient(
    const HostInfo& hostInfo) {
  auto qsfpServicePort = CmdGlobalOptions::getInstance()->getQsfpThriftPort();
  return createPlaintextClient<facebook::fboss::QsfpServiceAsyncClient>(
      hostInfo, qsfpServicePort);
}

std::unique_ptr<facebook::fboss::mka::MKAServiceAsyncClient> createMkaClient(
    const HostInfo& hostInfo) {
  auto port = CmdGlobalOptions::getInstance()->getMkaThriftPort();
  return createPlaintextClient<facebook::fboss::mka::MKAServiceAsyncClient>(
      hostInfo, port);
}

} // namespace facebook::fboss::utils
