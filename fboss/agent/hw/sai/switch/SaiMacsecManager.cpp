/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiMacsecManager.h"

#include <folly/logging/xlog.h>
#include "fboss/agent/FbossError.h"

namespace facebook::fboss {

SaiMacsecManager::SaiMacsecManager(SaiStore* saiStore) : saiStore_(saiStore) {}

SaiMacsecManager::~SaiMacsecManager() {}

MacsecSaiId SaiMacsecManager::addMacsec(
    sai_macsec_direction_t direction,
    bool physicalBypassEnable) {
  auto handle = getMacsecHandle(direction);
  if (handle) {
    throw FbossError(
        "Attempted to add macsec for direction that already has a macsec pipeline: ",
        direction,
        " SAI id: ",
        handle->macsec->adapterKey());
  }

  SaiMacsecTraits::CreateAttributes attributes = {
      direction, physicalBypassEnable};
  SaiMacsecTraits::AdapterHostKey key{direction};

  auto& macsecStore = saiStore_->get<SaiMacsecTraits>();
  auto saiMacsecObj = macsecStore.setObject(key, attributes);
  auto macsecHandle = std::make_unique<SaiMacsecHandle>();
  handle->macsec = saiMacsecObj;

  macsecHandles_.emplace(direction, std::move(macsecHandle));
  return macsecHandles_[direction]->macsec->adapterKey();
}

void SaiMacsecManager::removeMacsec(sai_macsec_direction_t direction) {
  auto itr = macsecHandles_.find(direction);
  if (itr == macsecHandles_.end()) {
    throw FbossError(
        "Attempted to remove non-existent macsec pipeline for direction: ",
        direction);
  }
  macsecHandles_.erase(itr);
  XLOG(INFO) << "removed macsec pipeline for direction " << direction;
}

const SaiMacsecHandle* FOLLY_NULLABLE
SaiMacsecManager::getMacsecHandle(sai_macsec_direction_t direction) const {
  return getMacsecHandleImpl(direction);
}
SaiMacsecHandle* FOLLY_NULLABLE
SaiMacsecManager::getMacsecHandle(sai_macsec_direction_t direction) {
  return getMacsecHandleImpl(direction);
}

SaiMacsecHandle* FOLLY_NULLABLE
SaiMacsecManager::getMacsecHandleImpl(sai_macsec_direction_t direction) const {
  auto itr = macsecHandles_.find(direction);
  if (itr == macsecHandles_.end()) {
    return nullptr;
  }
  if (!itr->second.get()) {
    XLOG(FATAL) << "Invalid null SaiMacsecHandle for direction " << direction;
  }
  return itr->second.get();
}

MacsecFlowSaiId SaiMacsecManager::addMacsecFlow(
    sai_macsec_direction_t direction) {
  auto flow = getMacsecFlow(direction);
  if (flow) {
    throw FbossError(
        "Attempted to add macsecFlow for direction that already exists: ",
        direction,
        " SAI id: ",
        flow->adapterKey());
  }

  auto macsecHandle = getMacsecHandle(direction);
  if (!macsecHandle) {
    throw FbossError(
        "Attempted to add macsecFlow for direction that has no macsec pipeline obj: ",
        direction);
  }

  SaiMacsecFlowTraits::CreateAttributes attributes = {
      direction,
  };
  SaiMacsecFlowTraits::AdapterHostKey key{direction};

  auto& store = saiStore_->get<SaiMacsecFlowTraits>();
  auto saiObj = store.setObject(key, attributes);

  macsecHandle->flow = std::move(saiObj);
  return macsecHandle->flow->adapterKey();
}

const SaiMacsecFlow* SaiMacsecManager::getMacsecFlow(
    sai_macsec_direction_t direction) const {
  return getMacsecFlowImpl(direction);
}
SaiMacsecFlow* SaiMacsecManager::getMacsecFlow(
    sai_macsec_direction_t direction) {
  return getMacsecFlowImpl(direction);
}

void SaiMacsecManager::removeMacsecFlow(sai_macsec_direction_t direction) {
  auto macsecHandle = getMacsecHandle(direction);
  if (!macsecHandle) {
    throw FbossError(
        "Attempted to remove macsecFlow for direction that has no macsec pipeline obj: ",
        direction);
  }

  if (!macsecHandle->flow) {
    throw FbossError(
        "Attempted to remove non-existent macsec flow for direction: ",
        direction);
  }
  macsecHandle->flow.reset();
  XLOG(INFO) << "removed macsec Flow for direction: " << direction;
}

SaiMacsecFlow* SaiMacsecManager::getMacsecFlowImpl(
    sai_macsec_direction_t direction) const {
  auto macsecHandle = getMacsecHandle(direction);
  if (!macsecHandle) {
    throw FbossError(
        "Attempted to get macsecFlow for direction that has no macsec pipeline obj: ",
        direction);
  }
  return macsecHandle->flow.get();
}

} // namespace facebook::fboss
