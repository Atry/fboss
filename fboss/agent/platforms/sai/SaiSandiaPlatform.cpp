/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/SaiSandiaPlatform.h"
#include "fboss/agent/hw/switch_asics/GaronneAsic.h"
#include "fboss/agent/platforms/common/sandia/SandiaPlatformMapping.h"

#include <algorithm>

namespace facebook::fboss {

SaiSandiaPlatform::SaiSandiaPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    folly::MacAddress localMac)
    : SaiTajoPlatform(
          std::move(productInfo),
          std::make_unique<SandiaPlatformMapping>(),
          localMac) {
  asic_ = std::make_unique<GaronneAsic>();
}

std::string SaiSandiaPlatform::getHwConfig() {
  return *config()->thrift.platform()->get_chip().get_asic().config();
}

SaiSandiaPlatform::~SaiSandiaPlatform() {}

HwAsic* SaiSandiaPlatform::getAsic() const {
  return asic_.get();
}

} // namespace facebook::fboss
