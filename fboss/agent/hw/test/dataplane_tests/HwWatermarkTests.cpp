/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fb303/ServiceData.h>
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwLinkStateDependentTest.h"
#include "fboss/agent/hw/test/HwSwitchEnsemble.h"
#include "fboss/agent/hw/test/HwTestPacketUtils.h"
#include "fboss/agent/hw/test/HwTestPortUtils.h"
#include "fboss/agent/hw/test/dataplane_tests/HwTestAqmUtils.h"
#include "fboss/agent/hw/test/dataplane_tests/HwTestOlympicUtils.h"
#include "fboss/agent/hw/test/dataplane_tests/HwTestQosUtils.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/test/EcmpSetupHelper.h"

#include <folly/IPAddress.h>

namespace facebook::fboss {

class HwWatermarkTest : public HwLinkStateDependentTest {
 private:
  cfg::SwitchConfig initialConfig() const override {
    auto cfg =
        utility::onePortPerVlanConfig(getHwSwitch(), masterLogicalPortIds());
    if (isSupported(HwAsic::Feature::L3_QOS)) {
      auto streamType =
          *(getPlatform()->getAsic()->getQueueStreamTypes(false).begin());
      utility::addOlympicQueueConfig(
          &cfg, streamType, getPlatform()->getAsic());
      utility::addOlympicQosMaps(cfg);
    }
    return cfg;
  }

  MacAddress kDstMac() const {
    return getPlatform()->getLocalMac();
  }
  void sendUdpPkt(
      uint8_t dscpVal,
      const folly::IPAddressV6& dst,
      int payloadSize,
      std::optional<PortID> port) {
    auto vlanId = utility::firstVlanID(initialConfig());
    auto intfMac = utility::getInterfaceMac(getProgrammedState(), vlanId);
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64NBO() + 1);

    auto kECT1 = 0x01; // ECN capable transport ECT(1)
    auto txPacket = utility::makeUDPTxPacket(
        getHwSwitch(),
        vlanId,
        srcMac,
        intfMac,
        folly::IPAddressV6("2620:0:1cfe:face:b00c::3"),
        dst,
        8000,
        8001,
        // Trailing 2 bits are for ECN, we do not want drops in
        // these queues due to any configured thresholds!
        static_cast<uint8_t>(dscpVal << 2 | kECT1),
        255,
        std::vector<uint8_t>(payloadSize, 0xff));
    if (port.has_value()) {
      getHwSwitch()->sendPacketOutOfPortSync(std::move(txPacket), *port);
    } else {
      getHwSwitch()->sendPacketSwitchedSync(std::move(txPacket));
    }
  }

  void
  assertWatermark(PortID port, int queueId, bool expectZero, int retries = 1) {
    EXPECT_TRUE(gotExpectedWatermark(port, queueId, expectZero, retries));
  }

  bool
  gotExpectedWatermark(PortID port, int queueId, bool expectZero, int retries) {
    do {
      auto queueWaterMarks = *getHwSwitchEnsemble()
                                  ->getLatestPortStats(port)
                                  .queueWatermarkBytes_();
      auto portName =
          getProgrammedState()->getPorts()->getPort(port)->getName();
      XLOG(DBG0) << "Port: " << portName << " queueId: " << queueId
                 << " Watermark: " << queueWaterMarks[queueId];

      auto watermarkAsExpected = (expectZero && !queueWaterMarks[queueId]) ||
          (!expectZero && queueWaterMarks[queueId]);
      if (watermarkAsExpected) {
        return true;
      }
      XLOG(DBG0) << " Retry ...";
      sleep(1);
    } while (--retries > 0);
    XLOG(INFO) << " Did not get expected watermark value";
    return false;
  }

  uint64_t getMinDeviceWatermarkValue() {
    uint64_t minDeviceWatermarkBytes{0};
    if (getAsic()->getAsicType() == HwAsic::AsicType::ASIC_TYPE_EBRO) {
      /*
       * Ebro will always have some internal buffer utilization even
       * when there is no traffic in the ASIC. The recommendation is
       * to consider atleast 100 buffers, translating to 100 x 384B
       * as steady state device watermark.
       */
      constexpr auto kEbroMinDeviceWatermarkBytes = 38400;
      minDeviceWatermarkBytes = kEbroMinDeviceWatermarkBytes;
    }
    return minDeviceWatermarkBytes;
  }

  bool gotExpectedDeviceWatermark(bool expectZero, int retries) {
    XLOG(DBG0) << "Expect zero watermark: " << std::boolalpha << expectZero;
    do {
      getHwSwitchEnsemble()->getLatestPortStats(masterLogicalPortIds()[0]);
      auto deviceWatermarkBytes =
          getHwSwitchEnsemble()->getHwSwitch()->getDeviceWatermarkBytes();
      XLOG(DBG0) << "Device watermark bytes: " << deviceWatermarkBytes;
      auto watermarkAsExpected =
          (expectZero &&
           (deviceWatermarkBytes <= getMinDeviceWatermarkValue())) ||
          (!expectZero &&
           (deviceWatermarkBytes > getMinDeviceWatermarkValue()));
      if (watermarkAsExpected) {
        return true;
      }
      XLOG(DBG0) << " Retry ...";
      sleep(1);
    } while (--retries > 0);

    XLOG(INFO) << " Did not get expected device watermark value";
    return false;
  }
  std::map<PortID, folly::IPAddressV6> getPort2DstIp() const {
    return {
        {masterLogicalPortIds()[0], kDestIp1()},
        {masterLogicalPortIds()[1], kDestIp2()},
    };
  }

 protected:
  folly::IPAddressV6 kDestIp1() const {
    return folly::IPAddressV6("2620:0:1cfe:face:b00c::4");
  }
  folly::IPAddressV6 kDestIp2() const {
    return folly::IPAddressV6("2620:0:1cfe:face:b00c::5");
  }

  /*
   * In practice, sending single packet usually (but not always) returned BST
   * value > 0 (usually 2, but  other times it was 0). Thus, send a large
   * number of packets to try to avoid the risk of flakiness.
   */
  void sendUdpPkts(
      uint8_t dscpVal,
      const folly::IPAddressV6& dstIp,
      int cnt = 100,
      int payloadSize = 6000,
      std::optional<PortID> port = std::nullopt) {
    for (int i = 0; i < cnt; i++) {
      sendUdpPkt(dscpVal, dstIp, payloadSize, port);
    }
  }
  void programRoutes() {
    for (auto portAndIp : getPort2DstIp()) {
      auto portDesc = PortDescriptor(portAndIp.first);
      utility::EcmpSetupTargetedPorts6 ecmpHelper6{getProgrammedState()};
      applyNewState(
          ecmpHelper6.resolveNextHops(getProgrammedState(), {portDesc}));
      ecmpHelper6.programRoutes(
          getRouteUpdater(),
          {portDesc},
          {Route<folly::IPAddressV6>::Prefix{portAndIp.second, 128}});
    }
  }

  void _setup(bool disableTtlDecrement = false) {
    auto vlanId = utility::firstVlanID(initialConfig());
    auto intfMac = utility::getInterfaceMac(getProgrammedState(), vlanId);
    auto kEcmpWidthForTest = 1;
    utility::EcmpSetupAnyNPorts6 ecmpHelper6{getProgrammedState(), intfMac};
    resolveNeigborAndProgramRoutes(ecmpHelper6, kEcmpWidthForTest);
    if (disableTtlDecrement) {
      utility::disableTTLDecrements(
          getHwSwitch(),
          ecmpHelper6.getRouterId(),
          ecmpHelper6.getNextHops()[0]);
    }
  }

  void assertDeviceWatermark(bool expectZero, int retries = 1) {
    EXPECT_TRUE(gotExpectedDeviceWatermark(expectZero, retries));
  }
  void runTest(int queueId) {
    if (!isSupported(HwAsic::Feature::L3_QOS)) {
      return;
    }

    auto setup = [this]() { programRoutes(); };
    auto verify = [this, queueId]() {
      auto dscpsForQueue = utility::kOlympicQueueToDscp().find(queueId)->second;
      for (auto portAndIp : getPort2DstIp()) {
        auto portName = getProgrammedState()
                            ->getPorts()
                            ->getPort(portAndIp.first)
                            ->getName();
        sendUdpPkts(dscpsForQueue[0], portAndIp.second);
        // Assert non zero watermark
        assertWatermark(portAndIp.first, queueId, false);
        // Assert zero watermark
        assertWatermark(portAndIp.first, queueId, true, 5);
        auto counters = fb303::fbData->getRegexCounters({folly::sformat(
            "buffer_watermark_ucast.{}.queue{}.*.p100.60", portName, queueId)});
        EXPECT_EQ(1, counters.size());
        // Unfortunately since  we use quantile stats, which compute
        // a MAX over a period, we can't really assert on the exact
        // value, just on its presence
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(HwWatermarkTest, VerifyDefaultQueue) {
  runTest(utility::kOlympicSilverQueueId);
}

TEST_F(HwWatermarkTest, VerifyNonDefaultQueue) {
  runTest(utility::kOlympicGoldQueueId);
}

// TODO - merge device watermark checking into the tests
// above once all platforms support device watermarks
TEST_F(HwWatermarkTest, VerifyDeviceWatermark) {
  auto setup = [this]() { _setup(true); };
  auto verify = [this]() {
    auto minPktsForLineRate =
        getHwSwitchEnsemble()->getMinPktsForLineRate(masterLogicalPortIds()[0]);
    sendUdpPkts(0, kDestIp1(), minPktsForLineRate);
    getHwSwitchEnsemble()->waitForLineRateOnPort(masterLogicalPortIds()[0]);
    // Assert non zero watermark
    assertDeviceWatermark(false);
    auto counters =
        fb303::fbData->getSelectedCounters({"buffer_watermark_device.p100.60"});
    // Unfortunately since  we use quantile stats, which compute
    // a MAX over a period, we can't really assert on the exact
    // value, just on its presence
    EXPECT_EQ(1, counters.size());

    // Now, break the loop to make sure traffic goes to zero!
    bringDownPort(masterLogicalPortIds()[0]);
    bringUpPort(masterLogicalPortIds()[0]);

    // Assert zero watermark
    assertDeviceWatermark(true, 5);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwWatermarkTest, VerifyDeviceWatermarkHigherThanQueueWatermark) {
  auto setup = [this]() {
    _setup(true);
    auto minPktsForLineRate =
        getHwSwitchEnsemble()->getMinPktsForLineRate(masterLogicalPortIds()[0]);
    // Sending traffic on 2 queues
    sendUdpPkts(
        utility::kOlympicQueueToDscp()
            .at(utility::kOlympicSilverQueueId)
            .front(),
        kDestIp1(),
        minPktsForLineRate / 2);
    sendUdpPkts(
        utility::kOlympicQueueToDscp().at(utility::kOlympicGoldQueueId).front(),
        kDestIp2(),
        minPktsForLineRate / 2);
    getHwSwitchEnsemble()->waitForLineRateOnPort(masterLogicalPortIds()[0]);
  };
  auto verify = [this]() {
    if (!isSupported(HwAsic::Feature::L3_QOS)) {
      return;
    }

    // Now we are at line rate on port, get queue watermark
    auto queueWaterMarks = *getHwSwitchEnsemble()
                                ->getLatestPortStats(masterLogicalPortIds()[0])
                                .queueWatermarkBytes_();
    // Get device watermark
    auto deviceWaterMark =
        getHwSwitchEnsemble()->getHwSwitch()->getDeviceWatermarkBytes();
    XLOG(DBG0) << "For port: " << masterLogicalPortIds()[0] << " Queue"
               << utility::kOlympicSilverQueueId << " watermark: "
               << queueWaterMarks.at(utility::kOlympicSilverQueueId)
               << ", Queue" << utility::kOlympicGoldQueueId << " watermark: "
               << queueWaterMarks.at(utility::kOlympicGoldQueueId)
               << ", Device watermark: " << deviceWaterMark;

    // Make sure that device watermark is > highest queue watermark
    EXPECT_GT(
        deviceWaterMark,
        std::max(
            queueWaterMarks.at(utility::kOlympicSilverQueueId),
            queueWaterMarks.at(utility::kOlympicGoldQueueId)));
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwWatermarkTest, VerifyQueueWatermarkAccuracy) {
  auto setup = []() {};
  auto verify = [this]() {
    if (!isSupported(HwAsic::Feature::L3_QOS)) {
#if defined(GTEST_SKIP)
      GTEST_SKIP();
#endif
      return;
    }

    /*
     * This test ensures that the queue watermark reported is accurate.
     * For this, we start by clearing any watermark from previous tests.
     * We need to know the exact number of bytes a packet will need while
     * it is in the ASIC queue. This size includes the headers + overhead
     * used in the ASIC for each packet. Once we know the number of bytes
     * that will be used per packet, with TX disable function, we ensure
     * that all the packets to be TXed is queued up, resulting in a high
     * watermark which is predictable. Then compare and make sure that
     * the watermark reported is as expected by the computed watermark
     * based on the number of bytes we expect in the queue.
     */
    constexpr auto kQueueId{0};
    constexpr auto kTxPacketPayloadLen{200};
    constexpr auto kNumberOfPacketsToSend{100};
    auto txPacketLen = kTxPacketPayloadLen + EthHdr::SIZE + IPv6Hdr::size() +
        UDPHeader::size();
    // Clear any watermark stats
    (void)getHwSwitchEnsemble()
        ->getLatestPortStats(masterLogicalPortIds()[0])
        .queueWatermarkBytes_();

    auto sendPackets = [=](PortID port, int numPacketsToSend) {
      sendUdpPkts(
          utility::kOlympicQueueToDscp().at(kQueueId).front(),
          kDestIp1(),
          numPacketsToSend,
          kTxPacketPayloadLen,
          port);
    };

    utility::sendPacketsWithQueueBuildup(
        sendPackets,
        getHwSwitchEnsemble(),
        masterLogicalPortIds()[0],
        kNumberOfPacketsToSend);

    auto portStats =
        getHwSwitchEnsemble()->getLatestPortStats(masterLogicalPortIds()[0]);
    auto queueWaterMarks = *portStats.queueWatermarkBytes_();
    auto expectedWatermarkBytes =
        utility::getEffectiveBytesPerPacket(getHwSwitch(), txPacketLen) *
        kNumberOfPacketsToSend;
    auto roundedWatermarkBytes = utility::getRoundedBufferThreshold(
        getHwSwitch(), expectedWatermarkBytes);
    XLOG(DBG0) << "Expected rounded watermark bytes: " << roundedWatermarkBytes
               << ", reported watermark bytes: "
               << queueWaterMarks.at(kQueueId);
    EXPECT_EQ(queueWaterMarks.at(kQueueId), roundedWatermarkBytes);
  };
  verifyAcrossWarmBoots(setup, verify);
}
} // namespace facebook::fboss
