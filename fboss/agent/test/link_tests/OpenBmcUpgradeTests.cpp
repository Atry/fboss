// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include "common/process/Process.h"
#include "fboss/agent/hw/test/HwTestEcmpUtils.h"
#include "fboss/agent/test/link_tests/LinkTest.h"
#include "fboss/lib/CommonUtils.h"

using namespace ::testing;
using namespace facebook::fboss;

DEFINE_string(oob_asset, "", "name/IP of the oob");
DEFINE_string(oob_flash_device_name, "", "name of the device to flash");
DEFINE_string(openbmc_password, "", "password to access the oob as root");

class OpenBmcUpgradeTest : public LinkTest {
 private:
  void rebootOob(int waitAfterRebootSeconds = 300) const {
    XLOG(INFO) << "Rebooting oob....";
    std::string rebootCmd = folly::sformat(
        "sshpass -p {} ssh root@{} reboot",
        FLAGS_openbmc_password,
        FLAGS_oob_asset);
    std::string resultStr;
    std::string errStr;
    if (!facebook::process::Process::execShellCmd(
            rebootCmd, &resultStr, &errStr)) {
      XLOG(ERR) << "Result str = " << resultStr;
      XLOG(ERR) << "Err str = " << errStr;
      if (errStr.find("closed by remote host") == std::string::npos) {
        throw FbossError("Reboot command failed : ", errStr);
      }
    }
    /* sleep override */
    sleep(waitAfterRebootSeconds);
  }

  void waitForSshAccessToOob() const {
    auto checkSsh = []() {
      std::string sshCmd = folly::sformat(
          "sshpass -p {} ssh root@{} ls",
          FLAGS_openbmc_password,
          FLAGS_oob_asset);
      std::string resultStr;
      std::string errStr;
      if (!facebook::process::Process::execShellCmd(
              sshCmd, &resultStr, &errStr)) {
        XLOG(ERR) << "Result str = " << resultStr;
        XLOG(ERR) << "Err str = " << errStr;
        return false;
      }
      return true;
    };

    checkWithRetry(checkSsh);
  }

 protected:
  void openBmcSanityCheck() const {
    XLOG(INFO) << "Checking ssh access to oob";
    waitForSshAccessToOob();
    std::string pingCmd = folly::sformat("ping6 -c 5 {}", FLAGS_oob_asset);
    std::string resultStr;
    std::string errStr;
    if (!facebook::process::Process::execShellCmd(
            pingCmd, &resultStr, &errStr)) {
      XLOG(ERR) << "Result str = " << resultStr;
      XLOG(ERR) << "Err str = " << errStr;
      throw FbossError("OpenBMC Sanity check failed : ", errStr);
    }
    XLOG(INFO) << "OpenBMC sanity check passed";
  }

  void upgradeOpenBmc() const {
    std::string upgradeCmd = folly::sformat(
        "sshpass -p {} ssh root@{} /opt/scripts/run_flashy.sh --device {}",
        FLAGS_openbmc_password,
        FLAGS_oob_asset,
        FLAGS_oob_flash_device_name);
    std::string resultStr;
    std::string errStr;
    if (!facebook::process::Process::execShellCmd(
            upgradeCmd, &resultStr, &errStr)) {
      XLOG(ERR) << "Result str = " << resultStr;
      XLOG(ERR) << "Err str = " << errStr;
      throw FbossError("OpenBMC upgrade failed : ", errStr);
    }
    // Reboot OOB
    rebootOob();
    XLOG(INFO) << "OpenBMC upgrade successful! Starting OpenBMC sanity check";
    openBmcSanityCheck();

    XLOG(INFO) << "OpenBMC version after upgrade : " << openBmcVersion();
  }

  std::string openBmcVersion() const {
    std::string sshCmd = folly::sformat(
        "sshpass -p {} ssh root@{} head -n 1 /etc/issue",
        FLAGS_openbmc_password,
        FLAGS_oob_asset);
    std::string resultStr;
    std::string errStr;
    if (!facebook::process::Process::execShellCmd(
            sshCmd, &resultStr, &errStr)) {
      XLOG(ERR) << "Result str = " << resultStr;
      XLOG(ERR) << "Err str = " << errStr;
      throw FbossError("Reading OpenBMC version failed : ", errStr);
    }
    return resultStr;
  }
};

TEST_F(OpenBmcUpgradeTest, openBmcHitlessUpgrade) {
  // Do an initial sanity check on the OpenBmc
  openBmcSanityCheck();
  XLOG(INFO) << "OpenBMC version before upgrade : " << openBmcVersion();

  // Start traffic
  createL3DataplaneFlood();
  // Assert traffic is clean before upgrading OpenBMC
  assertNoInDiscards();

  // Upgrade OpenBMC
  upgradeOpenBmc();

  // Assert no traffic loss and no ecmp shrink. If ports flap
  // these conditions will not be true
  assertNoInDiscards();
  auto ecmpSizeInSw = getVlanOwningCabledPorts().size();
  EXPECT_EQ(
      utility::getEcmpSizeInHw(
          sw()->getHw(),
          {folly::IPAddress("::"), 0},
          RouterID(0),
          ecmpSizeInSw),
      ecmpSizeInSw);
}
