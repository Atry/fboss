// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/StateObserver.h"
#include "fboss/agent/state/StateDelta.h"

namespace facebook::fboss {
class SwSwitch;

class AclNexthopHandler : public StateObserver {
 public:
  explicit AclNexthopHandler(SwSwitch* sw);
  ~AclNexthopHandler() override;

  void stateUpdated(const StateDelta& delta) override;

 private:
  std::shared_ptr<SwitchState> handleUpdate(
      const std::shared_ptr<SwitchState>& state);
  std::shared_ptr<AclMap> updateAcls(std::shared_ptr<SwitchState>& newState);
  void resolveActionNexthops(MatchAction& action);
  AclEntry* FOLLY_NULLABLE updateAcl(
      const std::shared_ptr<AclEntry>& origAclEntry,
      std::shared_ptr<SwitchState>& newState);
  bool hasAclChanges(const StateDelta& delta);

  SwSwitch* sw_;
};

} // namespace facebook::fboss
