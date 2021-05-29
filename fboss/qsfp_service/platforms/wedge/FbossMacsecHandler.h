// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/mka_service/handlers/MacsecHandler.h"
#include "fboss/qsfp_service/TransceiverManager.h"

namespace facebook {
namespace fboss {

class FbossMacsecHandler : public mka::MacsecHandler {
 public:
  // Explicit constructor taking the wedgeManager object
  explicit FbossMacsecHandler(TransceiverManager* wedgeManager)
      : wedgeManager_(wedgeManager) {}

  // Virtual function for sakInstallRx. The Macsec supporting platform should
  // implement this API in the subclass
  virtual bool sakInstallRx(
      const mka::MKASak& /* sak */,
      const mka::MKASci& /* sci */) override {
    return false;
  }

  // Virtual function for sakInstallTx. The Macsec supporting platform should
  // implement this API in the subclass
  virtual bool sakInstallTx(const mka::MKASak& /* sak */) override {
    return false;
  }

  // Virtual function for sakDeleteRx. The Macsec supporting platform should
  // implement this API in the subclass
  virtual bool sakDeleteRx(
      const mka::MKASak& /* sak */,
      const mka::MKASci& /* sci */) override {
    return false;
  }

  // Virtual function for sakDelete. The Macsec supporting platform should
  // implement this API in the subclass
  virtual bool sakDelete(const mka::MKASak& /* sak */) override {
    return false;
  }

  // Virtual function for sakHealthCheck. The Macsec supporting platform should
  // implement this API in the subclass
  virtual mka::MKASakHealthResponse sakHealthCheck(
      const mka::MKASak& /* sak */) override {
    mka::MKASakHealthResponse result;
    return result;
  }

 protected:
  // TransceiverManager or WedgeManager object pointer passed by QsfpService
  // main
  TransceiverManager* wedgeManager_;
};

} // namespace fboss
} // namespace facebook
