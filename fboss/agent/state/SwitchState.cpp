/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclTableGroupMap.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/AggregatePortMap.h"
#include "fboss/agent/state/ControlPlane.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/LabelForwardingInformationBase.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/QosPolicyMap.h"
#include "fboss/agent/state/SflowCollectorMap.h"
#include "fboss/agent/state/SwitchSettings.h"
#include "fboss/agent/state/Transceiver.h"
#include "fboss/agent/state/TransceiverMap.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"

#include "fboss/agent/state/NodeBase-defs.h"

using std::make_shared;
using std::shared_ptr;
using std::chrono::seconds;

namespace {
constexpr auto kInterfaces = "interfaces";
constexpr auto kPorts = "ports";
constexpr auto kVlans = "vlans";
constexpr auto kDefaultVlan = "defaultVlan";
constexpr auto kAcls = "acls";
constexpr auto kSflowCollectors = "sFlowCollectors";
constexpr auto kControlPlane = "controlPlane";
constexpr auto kQosPolicies = "qosPolicies";
constexpr auto kArpTimeout = "arpTimeout";
constexpr auto kNdpTimeout = "ndpTimeout";
constexpr auto kArpAgerInterval = "arpAgerInterval";
constexpr auto kMaxNeighborProbes = "maxNeighborProbes";
constexpr auto kStaleEntryInterval = "staleEntryInterval";
constexpr auto kLoadBalancers = "loadBalancers";
constexpr auto kMirrors = "mirrors";
constexpr auto kAggregatePorts = "aggregatePorts";
constexpr auto kLabelForwardingInformationBase = "labelFib";
constexpr auto kSwitchSettings = "switchSettings";
constexpr auto kDefaultDataplaneQosPolicy = "defaultDataPlaneQosPolicy";
constexpr auto kQcmCfg = "qcmConfig";
constexpr auto kBufferPoolCfgs = "bufferPoolConfigs";
constexpr auto kFibs = "fibs";
constexpr auto kTransceivers = "transceivers";
constexpr auto kAclTableGroups = "aclTableGroups";
} // namespace

// TODO: it might be worth splitting up limits for ecmp/ucmp
DEFINE_uint32(
    ecmp_width,
    64,
    "Max ecmp width. Also implies ucmp normalization factor");

namespace facebook::fboss {

SwitchStateFields::SwitchStateFields()
    : ports(make_shared<PortMap>()),
      aggPorts(make_shared<AggregatePortMap>()),
      vlans(make_shared<VlanMap>()),
      interfaces(make_shared<InterfaceMap>()),
      acls(make_shared<AclMap>()),
      aclTableGroups(make_shared<AclTableGroupMap>()),
      sFlowCollectors(make_shared<SflowCollectorMap>()),
      qosPolicies(make_shared<QosPolicyMap>()),
      controlPlane(make_shared<ControlPlane>()),
      loadBalancers(make_shared<LoadBalancerMap>()),
      mirrors(make_shared<MirrorMap>()),
      fibs(make_shared<ForwardingInformationBaseMap>()),
      labelFib(make_shared<LabelForwardingInformationBase>()),
      switchSettings(make_shared<SwitchSettings>()),
      transceivers(make_shared<TransceiverMap>()) {}

state::SwitchState SwitchStateFields::toThrift() const {
  auto state = state::SwitchState();
  state.portMap() = ports->toThrift();
  state.vlanMap() = vlans->toThrift();
  state.aclMap() = acls->toThrift();
  return state;
}

SwitchStateFields SwitchStateFields::fromThrift(
    const state::SwitchState& state) {
  auto fields = SwitchStateFields();
  fields.ports = PortMap::fromThrift(state.get_portMap());
  fields.vlans = VlanMap::fromThrift(state.get_vlanMap());
  fields.acls = AclMap::fromThrift(state.get_aclMap());
  return fields;
}

bool SwitchStateFields::operator==(const SwitchStateFields& other) const {
  // TODO: add rest of fields as we convert them to thrifty
  return std::tie(*ports, *vlans, *acls) ==
      std::tie(*other.ports, *other.vlans, *other.acls);
}

folly::dynamic SwitchStateFields::toFollyDynamic() const {
  folly::dynamic switchState = folly::dynamic::object;
  switchState[kInterfaces] = interfaces->toFollyDynamic();
  switchState[kPorts] = ports->toFollyDynamic();
  switchState[kVlans] = vlans->toFollyDynamic();
  switchState[kAcls] = acls->toFollyDynamic();
  switchState[kSflowCollectors] = sFlowCollectors->toFollyDynamic();
  switchState[kDefaultVlan] = static_cast<uint32_t>(defaultVlan);
  switchState[kControlPlane] = controlPlane->toFollyDynamic();
  switchState[kLoadBalancers] = loadBalancers->toFollyDynamic();
  switchState[kMirrors] = mirrors->toFollyDynamic();
  switchState[kAggregatePorts] = aggPorts->toFollyDynamic();
  switchState[kLabelForwardingInformationBase] = labelFib->toFollyDynamic();
  switchState[kSwitchSettings] = switchSettings->toFollyDynamic();
  if (qcmCfg) {
    switchState[kQcmCfg] = qcmCfg->toFollyDynamic();
  }
  if (bufferPoolCfgs) {
    switchState[kBufferPoolCfgs] = bufferPoolCfgs->toFollyDynamic();
  }
  if (defaultDataPlaneQosPolicy) {
    switchState[kDefaultDataplaneQosPolicy] =
        defaultDataPlaneQosPolicy->toFollyDynamic();
  }
  switchState[kQosPolicies] = qosPolicies->toFollyDynamic();
  switchState[kFibs] = fibs->toFollyDynamic();
  switchState[kTransceivers] = transceivers->toFollyDynamic();
  if (aclTableGroups) {
    switchState[kAclTableGroups] = aclTableGroups->toFollyDynamic();
  }
  return switchState;
}

SwitchStateFields SwitchStateFields::fromFollyDynamic(
    const folly::dynamic& swJson) {
  SwitchStateFields switchState;
  switchState.interfaces = InterfaceMap::fromFollyDynamic(swJson[kInterfaces]);
  switchState.ports = PortMap::fromFollyDynamic(swJson[kPorts]);
  switchState.vlans = VlanMap::fromFollyDynamic(swJson[kVlans]);
  switchState.acls = AclMap::fromFollyDynamic(swJson[kAcls]);
  if (swJson.count(kSflowCollectors) > 0) {
    switchState.sFlowCollectors =
        SflowCollectorMap::fromFollyDynamic(swJson[kSflowCollectors]);
  }
  switchState.defaultVlan = VlanID(swJson[kDefaultVlan].asInt());
  if (swJson.find(kQosPolicies) != swJson.items().end()) {
    switchState.qosPolicies =
        QosPolicyMap::fromFollyDynamic(swJson[kQosPolicies]);
  }
  if (swJson.find(kControlPlane) != swJson.items().end()) {
    switchState.controlPlane =
        ControlPlane::fromFollyDynamic(swJson[kControlPlane]);
  }
  if (swJson.find(kLoadBalancers) != swJson.items().end()) {
    switchState.loadBalancers =
        LoadBalancerMap::fromFollyDynamic(swJson[kLoadBalancers]);
  }
  if (swJson.find(kMirrors) != swJson.items().end()) {
    switchState.mirrors = MirrorMap::fromFollyDynamic(swJson[kMirrors]);
  }
  if (swJson.find(kAggregatePorts) != swJson.items().end()) {
    switchState.aggPorts =
        AggregatePortMap::fromFollyDynamic(swJson[kAggregatePorts]);
  }
  if (swJson.find(kLabelForwardingInformationBase) != swJson.items().end()) {
    switchState.labelFib = LabelForwardingInformationBase::fromFollyDynamic(
        swJson[kLabelForwardingInformationBase]);
  }
  if (swJson.find(kSwitchSettings) != swJson.items().end()) {
    switchState.switchSettings =
        SwitchSettings::fromFollyDynamic(swJson[kSwitchSettings]);
  }

  if (swJson.find(kDefaultDataplaneQosPolicy) != swJson.items().end()) {
    switchState.defaultDataPlaneQosPolicy =
        QosPolicy::fromFollyDynamic(swJson[kDefaultDataplaneQosPolicy]);
    auto name = switchState.defaultDataPlaneQosPolicy->getName();
    /* for backward compatibility, this policy is also kept in qos policy map.
     * remove it, if it exists */
    /* TODO(pshaikh): remove this after one pushes, after next push, logic
     * that keeps  default qos policy in qos policy map will be removed. */
    switchState.qosPolicies->removeNodeIf(name);
  }

  if (swJson.find(kQcmCfg) != swJson.items().end()) {
    switchState.qcmCfg = QcmCfg::fromFollyDynamic(swJson[kQcmCfg]);
  }
  if (swJson.find(kBufferPoolCfgs) != swJson.items().end()) {
    switchState.bufferPoolCfgs =
        BufferPoolCfgMap::fromFollyDynamic(swJson[kBufferPoolCfgs]);
  }
  if (swJson.find(kFibs) != swJson.items().end()) {
    switchState.fibs =
        ForwardingInformationBaseMap::fromFollyDynamic(swJson[kFibs]);
  }
  // TODO(joseph5wu) Will eventually make transceivers as a mandatory field
  if (const auto& values = swJson.find(kTransceivers);
      values != swJson.items().end()) {
    switchState.transceivers = TransceiverMap::fromFollyDynamic(values->second);
  }

  if (swJson.find(kAclTableGroups) != swJson.items().end()) {
    switchState.aclTableGroups =
        AclTableGroupMap::fromFollyDynamic(swJson[kAclTableGroups]);
  }

  // TODO verify that created state here is internally consistent t4155406
  return switchState;
}

SwitchState::SwitchState() {}

SwitchState::~SwitchState() {}

void SwitchState::modify(std::shared_ptr<SwitchState>* state) {
  if (!(*state)->isPublished()) {
    return;
  }
  *state = (*state)->clone();
}

std::shared_ptr<Port> SwitchState::getPort(PortID id) const {
  return getFields()->ports->getPort(id);
}

void SwitchState::registerPort(PortID id, const std::string& name) {
  writableFields()->ports->registerPort(id, name);
}

void SwitchState::addPort(const std::shared_ptr<Port>& port) {
  writableFields()->ports->addPort(port);
}

void SwitchState::resetPorts(std::shared_ptr<PortMap> ports) {
  writableFields()->ports.swap(ports);
}

void SwitchState::resetVlans(std::shared_ptr<VlanMap> vlans) {
  writableFields()->vlans.swap(vlans);
}

void SwitchState::addVlan(const std::shared_ptr<Vlan>& vlan) {
  auto* fields = writableFields();
  // For ease-of-use, automatically clone the VlanMap if we are still
  // pointing to a published map.
  if (fields->vlans->isPublished()) {
    fields->vlans = fields->vlans->clone();
  }
  fields->vlans->addVlan(vlan);
}

void SwitchState::setDefaultVlan(VlanID id) {
  writableFields()->defaultVlan = id;
}

void SwitchState::setArpTimeout(seconds timeout) {
  writableFields()->arpTimeout = timeout;
}

void SwitchState::setNdpTimeout(seconds timeout) {
  writableFields()->ndpTimeout = timeout;
}

void SwitchState::setArpAgerInterval(seconds interval) {
  writableFields()->arpAgerInterval = interval;
}

void SwitchState::setMaxNeighborProbes(uint32_t maxNeighborProbes) {
  writableFields()->maxNeighborProbes = maxNeighborProbes;
}

void SwitchState::setStaleEntryInterval(seconds interval) {
  writableFields()->staleEntryInterval = interval;
}

void SwitchState::addIntf(const std::shared_ptr<Interface>& intf) {
  auto* fields = writableFields();
  // For ease-of-use, automatically clone the InterfaceMap if we are still
  // pointing to a published map.
  if (fields->interfaces->isPublished()) {
    fields->interfaces = fields->interfaces->clone();
  }
  fields->interfaces->addInterface(intf);
}

void SwitchState::resetIntfs(std::shared_ptr<InterfaceMap> intfs) {
  writableFields()->interfaces.swap(intfs);
}

void SwitchState::addAcl(const std::shared_ptr<AclEntry>& acl) {
  auto* fields = writableFields();
  // For ease-of-use, automatically clone the AclMap if we are still
  // pointing to a published map.
  if (fields->acls->isPublished()) {
    fields->acls = fields->acls->clone();
  }
  fields->acls->addEntry(acl);
}

std::shared_ptr<AclEntry> SwitchState::getAcl(const std::string& name) const {
  return getFields()->acls->getEntryIf(name);
}

void SwitchState::resetAcls(std::shared_ptr<AclMap> acls) {
  writableFields()->acls.swap(acls);
}

void SwitchState::resetAclTableGroups(
    std::shared_ptr<AclTableGroupMap> aclTableGroups) {
  writableFields()->aclTableGroups.swap(aclTableGroups);
}

void SwitchState::resetAggregatePorts(
    std::shared_ptr<AggregatePortMap> aggPorts) {
  writableFields()->aggPorts.swap(aggPorts);
}

void SwitchState::resetSflowCollectors(
    const std::shared_ptr<SflowCollectorMap>& collectors) {
  writableFields()->sFlowCollectors = collectors;
}

void SwitchState::resetQosPolicies(std::shared_ptr<QosPolicyMap> qosPolicies) {
  writableFields()->qosPolicies = qosPolicies;
}

void SwitchState::resetControlPlane(
    std::shared_ptr<ControlPlane> controlPlane) {
  writableFields()->controlPlane = controlPlane;
}

void SwitchState::resetLoadBalancers(
    std::shared_ptr<LoadBalancerMap> loadBalancers) {
  writableFields()->loadBalancers.swap(loadBalancers);
}

void SwitchState::resetSwitchSettings(
    std::shared_ptr<SwitchSettings> switchSettings) {
  writableFields()->switchSettings = switchSettings;
}

void SwitchState::resetQcmCfg(std::shared_ptr<QcmCfg> qcmCfg) {
  writableFields()->qcmCfg = qcmCfg;
}

void SwitchState::resetBufferPoolCfgs(std::shared_ptr<BufferPoolCfgMap> cfgs) {
  writableFields()->bufferPoolCfgs = cfgs;
}

const std::shared_ptr<LoadBalancerMap>& SwitchState::getLoadBalancers() const {
  return getFields()->loadBalancers;
}

void SwitchState::resetMirrors(std::shared_ptr<MirrorMap> mirrors) {
  writableFields()->mirrors.swap(mirrors);
}

const std::shared_ptr<MirrorMap>& SwitchState::getMirrors() const {
  return getFields()->mirrors;
}

const std::shared_ptr<ForwardingInformationBaseMap>& SwitchState::getFibs()
    const {
  return getFields()->fibs;
}

void SwitchState::resetLabelForwardingInformationBase(
    std::shared_ptr<LabelForwardingInformationBase> labelFib) {
  writableFields()->labelFib.swap(labelFib);
}

void SwitchState::resetForwardingInformationBases(
    std::shared_ptr<ForwardingInformationBaseMap> fibs) {
  writableFields()->fibs.swap(fibs);
}

void SwitchState::addTransceiver(
    const std::shared_ptr<Transceiver>& transceiver) {
  auto* fields = writableFields();
  // For ease-of-use, automatically clone the TransceiverMap if we are still
  // pointing to a published map.
  if (fields->transceivers->isPublished()) {
    fields->transceivers = fields->transceivers->clone();
  }
  fields->transceivers->addTransceiver(transceiver);
}

void SwitchState::resetTransceivers(
    std::shared_ptr<TransceiverMap> transceivers) {
  writableFields()->transceivers.swap(transceivers);
}

std::shared_ptr<AclTableMap> SwitchState::getAclTablesForStage(
    cfg::AclStage aclStage) const {
  if (getAclTableGroups() &&
      getAclTableGroups()->getAclTableGroupIf(aclStage) &&
      getAclTableGroups()->getAclTableGroup(aclStage)->getAclTableMap()) {
    return getAclTableGroups()->getAclTableGroup(aclStage)->getAclTableMap();
  }

  return nullptr;
}

std::shared_ptr<AclMap> SwitchState::getAclsForTable(
    cfg::AclStage aclStage,
    const std::string& tableName) const {
  auto aclTableMap = getAclTablesForStage(aclStage);

  if (aclTableMap && aclTableMap->getTableIf(tableName)) {
    return aclTableMap->getTable(tableName)->getAclMap();
  }

  return nullptr;
}

std::shared_ptr<SwitchState> SwitchState::modifyTransceivers(
    const std::shared_ptr<SwitchState>& state,
    const std::unordered_map<TransceiverID, TransceiverInfo>& currentTcvrs) {
  auto origTcvrs = state->getTransceivers();
  TransceiverMap::NodeContainer newTcvrs;
  bool changed = false;
  for (const auto& tcvrInfo : currentTcvrs) {
    auto origTcvr = origTcvrs->getTransceiverIf(tcvrInfo.first);
    auto newTcvr = Transceiver::createPresentTransceiver(tcvrInfo.second);
    if (!newTcvr) {
      // If the transceiver used to be present but now was removed
      changed |= (origTcvr != nullptr);
      continue;
    } else {
      if (origTcvr && *origTcvr == *newTcvr) {
        newTcvrs.emplace(origTcvr->getID(), origTcvr);
      } else {
        changed = true;
        newTcvrs.emplace(newTcvr->getID(), newTcvr);
      }
    }
  }

  if (changed) {
    XLOG(DBG2) << "New TransceiverMap has " << newTcvrs.size()
               << " present transceivers, original map has "
               << origTcvrs->size();
    auto newState = state->clone();
    newState->resetTransceivers(origTcvrs->clone(newTcvrs));
    return newState;
  } else {
    XLOG(DBG2)
        << "Current transceivers from QsfpCache has the same transceiver size:"
        << origTcvrs->size()
        << ", no need to reset TransceiverMap in current SwitchState";
    return nullptr;
  }
}

template class NodeBaseT<SwitchState, SwitchStateFields>;

} // namespace facebook::fboss
