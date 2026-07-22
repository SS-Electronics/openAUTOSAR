// SPDX-License-Identifier: MIT

#include "openautosar/runtime/time_network_manager.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace openautosar::runtime::tnm {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"time-network-management", message};
}

[[nodiscard]] std::uint64_t AbsoluteDifference(
  std::int64_t left,
  std::int64_t right) noexcept {
  return left >= right ? static_cast<std::uint64_t>(left - right)
                       : static_cast<std::uint64_t>(right - left);
}

[[nodiscard]] bool ContainsState(
  const std::vector<FunctionGroupState>& states,
  FunctionGroupState state) {
  return states.empty() ||
         std::find(states.begin(), states.end(), state) != states.end();
}

[[nodiscard]] bool AddressMatches(std::string_view value, std::string_view prefix) {
  return prefix.empty() || prefix == "*" || value.rfind(prefix, 0U) == 0U;
}

[[nodiscard]] const char* NftProtocol(NetworkProtocol protocol) noexcept {
  switch (protocol) {
    case NetworkProtocol::kUdp:
      return "udp";
    case NetworkProtocol::kTcp:
      return "tcp";
    case NetworkProtocol::kCan:
      return "meta";
    case NetworkProtocol::kLocal:
      return "meta";
  }

  return "meta";
}

[[nodiscard]] std::string RuleCommandFor(const EndpointPolicy& endpoint) {
  std::ostringstream command;
  command << "nft add rule inet openautosar egress oifname \""
          << endpoint.interface_name << "\" ";
  if (endpoint.protocol == NetworkProtocol::kUdp ||
      endpoint.protocol == NetworkProtocol::kTcp) {
    command << NftProtocol(endpoint.protocol) << " dport "
            << endpoint.port_min << "-" << endpoint.port_max << ' ';
  }
  command << "accept comment \"" << endpoint.endpoint_id << "\"";
  return command.str();
}

}  // namespace

core::Result<std::uint64_t> VirtualClock::AdvanceBy(std::uint64_t delta_ns) {
  now_ns_ += delta_ns;
  return core::Result<std::uint64_t>::FromValue(now_ns_);
}

core::Result<std::uint64_t> VirtualClock::AdvanceTo(std::uint64_t next_ns) {
  if (next_ns < now_ns_) {
    return core::Result<std::uint64_t>::FromError(
      MakeError("virtual monotonic clock cannot move backward"));
  }

  now_ns_ = next_ns;
  return core::Result<std::uint64_t>::FromValue(now_ns_);
}

core::Result<bool> TimeManager::RegisterClockDomain(ClockDomainConfig config) {
  if (config.domain_id.empty()) {
    return core::Result<bool>::FromError(MakeError("clock domain id is empty"));
  }

  if (config.loss_of_sync_timeout_ns == 0U) {
    return core::Result<bool>::FromError(MakeError("loss-of-sync timeout is invalid"));
  }

  auto existing = std::find_if(
    domains_.begin(),
    domains_.end(),
    [&config](const ClockDomainRecord& record) {
      return record.config.domain_id == config.domain_id;
    });
  if (existing != domains_.end()) {
    return core::Result<bool>::FromError(MakeError("clock domain already exists"));
  }

  TimeSnapshot snapshot;
  snapshot.domain_id = config.domain_id;
  domains_.push_back({.config = std::move(config), .snapshot = std::move(snapshot)});
  return core::Result<bool>::FromValue(true);
}

core::Result<TimeSnapshot> TimeManager::ObserveTimeSync(TimeSyncSample sample) {
  if (sample.source.empty()) {
    return core::Result<TimeSnapshot>::FromError(MakeError("time sync source is empty"));
  }

  auto record = FindMutable(sample.domain_id);
  if (!record) {
    return core::Result<TimeSnapshot>::FromError(record.Error());
  }

  auto& domain = *record.Value();
  if (domain.has_sample && sample.monotonic_ns < domain.snapshot.monotonic_ns) {
    return core::Result<TimeSnapshot>::FromError(
      MakeError("monotonic time moved backward"));
  }

  auto quality = ClockQuality::kSynchronized;
  if (sample.uncertainty_ns > domain.config.max_uncertainty_ns) {
    quality = ClockQuality::kDegraded;
  }

  if (domain.has_sample) {
    const auto elapsed = sample.monotonic_ns - domain.snapshot.monotonic_ns;
    const auto expected_global =
      domain.snapshot.global_time_ns + static_cast<std::int64_t>(elapsed);
    const auto jump = AbsoluteDifference(sample.global_time_ns, expected_global);
    if (jump > domain.config.jump_threshold_ns) {
      if (domain.config.jump_policy == TimeJumpPolicy::kReject) {
        return core::Result<TimeSnapshot>::FromError(
          MakeError("global time jump rejected by policy"));
      }
      if (domain.config.jump_policy == TimeJumpPolicy::kDegrade) {
        quality = ClockQuality::kDegraded;
      }
    }
  }

  domain.snapshot.monotonic_ns = sample.monotonic_ns;
  domain.snapshot.global_time_ns = sample.global_time_ns;
  domain.snapshot.uncertainty_ns = sample.uncertainty_ns;
  domain.snapshot.quality = quality;
  domain.snapshot.synchronized = true;
  domain.snapshot.source = std::move(sample.source);
  domain.has_sample = true;

  return core::Result<TimeSnapshot>::FromValue(domain.snapshot);
}

core::Result<TimeSnapshot> TimeManager::EvaluateLossOfSync(
  std::string_view domain_id,
  std::uint64_t monotonic_now_ns) {
  auto record = FindMutable(domain_id);
  if (!record) {
    return core::Result<TimeSnapshot>::FromError(record.Error());
  }

  auto& domain = *record.Value();
  if (!domain.has_sample) {
    return core::Result<TimeSnapshot>::FromValue(domain.snapshot);
  }

  if (monotonic_now_ns < domain.snapshot.monotonic_ns) {
    return core::Result<TimeSnapshot>::FromError(
      MakeError("loss-of-sync evaluation time moved backward"));
  }

  const auto elapsed = monotonic_now_ns - domain.snapshot.monotonic_ns;
  if (elapsed > domain.config.loss_of_sync_timeout_ns) {
    domain.snapshot.quality = ClockQuality::kLostSync;
    domain.snapshot.synchronized = false;
  }

  return core::Result<TimeSnapshot>::FromValue(domain.snapshot);
}

core::Result<TimeSnapshot> TimeManager::Snapshot(std::string_view domain_id) const {
  auto record = Find(domain_id);
  if (!record) {
    return core::Result<TimeSnapshot>::FromError(record.Error());
  }

  return core::Result<TimeSnapshot>::FromValue(record.Value()->snapshot);
}

std::vector<TimeSnapshot> TimeManager::Snapshots() const {
  std::vector<TimeSnapshot> snapshots;
  snapshots.reserve(domains_.size());
  for (const auto& domain : domains_) {
    snapshots.push_back(domain.snapshot);
  }
  return snapshots;
}

core::Result<TimeManager::ClockDomainRecord*> TimeManager::FindMutable(
  std::string_view domain_id) {
  auto iter = std::find_if(
    domains_.begin(),
    domains_.end(),
    [domain_id](const ClockDomainRecord& record) {
      return record.config.domain_id == domain_id;
    });
  if (iter == domains_.end()) {
    return core::Result<ClockDomainRecord*>::FromError(MakeError("clock domain is missing"));
  }

  return core::Result<ClockDomainRecord*>::FromValue(&(*iter));
}

core::Result<const TimeManager::ClockDomainRecord*> TimeManager::Find(
  std::string_view domain_id) const {
  auto iter = std::find_if(
    domains_.begin(),
    domains_.end(),
    [domain_id](const ClockDomainRecord& record) {
      return record.config.domain_id == domain_id;
    });
  if (iter == domains_.end()) {
    return core::Result<const ClockDomainRecord*>::FromError(
      MakeError("clock domain is missing"));
  }

  return core::Result<const ClockDomainRecord*>::FromValue(&(*iter));
}

core::Result<bool> NetworkManager::RegisterInterface(NetworkInterfacePolicy policy) {
  if (policy.interface_name.empty()) {
    return core::Result<bool>::FromError(MakeError("network interface name is empty"));
  }

  if (policy.recovery_timeout_ms == 0U) {
    return core::Result<bool>::FromError(MakeError("network recovery timeout is invalid"));
  }

  auto existing = FindInterface(policy.interface_name);
  if (existing) {
    return core::Result<bool>::FromError(MakeError("network interface already exists"));
  }

  NetworkInterfaceStatus status;
  status.interface_name = policy.interface_name;
  interfaces_.push_back({.policy = std::move(policy), .status = std::move(status)});
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> NetworkManager::UpdateInterfaceState(NetworkInterfaceStatus status) {
  if (status.interface_name.empty()) {
    return core::Result<bool>::FromError(MakeError("network status interface is empty"));
  }

  auto record = FindInterfaceMutable(status.interface_name);
  if (!record) {
    return core::Result<bool>::FromError(record.Error());
  }

  record.Value()->status = std::move(status);
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> NetworkManager::RegisterEndpointPolicy(EndpointPolicy policy) {
  if (policy.endpoint_id.empty() || policy.interface_name.empty()) {
    return core::Result<bool>::FromError(MakeError("endpoint policy identity is invalid"));
  }

  if (policy.port_min > policy.port_max) {
    return core::Result<bool>::FromError(MakeError("endpoint port range is invalid"));
  }

  auto interface = FindInterface(policy.interface_name);
  if (!interface) {
    return core::Result<bool>::FromError(interface.Error());
  }

  auto existing = FindEndpoint(policy.endpoint_id);
  if (existing) {
    return core::Result<bool>::FromError(MakeError("endpoint policy already exists"));
  }

  endpoints_.push_back(std::move(policy));
  return core::Result<bool>::FromValue(true);
}

core::Result<NetworkDecision> NetworkManager::Evaluate(
  const CommunicationRequirement& requirement) const {
  if (requirement.application_id.empty()) {
    return core::Result<NetworkDecision>::FromError(
      MakeError("communication requirement application id is empty"));
  }

  auto endpoint = FindEndpoint(requirement.endpoint_id);
  if (!endpoint) {
    return core::Result<NetworkDecision>::FromError(endpoint.Error());
  }

  auto interface = FindInterface(endpoint.Value()->interface_name);
  if (!interface) {
    return core::Result<NetworkDecision>::FromError(interface.Error());
  }

  NetworkDecision decision;
  decision.interface_name = interface.Value()->status.interface_name;
  decision.interface_state = interface.Value()->status.state;

  if (interface.Value()->status.state != NetworkInterfaceState::kReady) {
    decision.reason = "network interface is not ready";
    return core::Result<NetworkDecision>::FromValue(decision);
  }

  if (!ContainsState(endpoint.Value()->function_groups, requirement.function_group) ||
      !ContainsState(interface.Value()->policy.function_groups, requirement.function_group)) {
    decision.reason = "function-group state is not allowed for endpoint";
    return core::Result<NetworkDecision>::FromValue(decision);
  }

  if (requirement.update_mode && !endpoint.Value()->allow_in_update_mode) {
    decision.reason = "endpoint is blocked during update mode";
    return core::Result<NetworkDecision>::FromValue(decision);
  }

  if (requirement.protocol != endpoint.Value()->protocol) {
    decision.reason = "protocol does not match endpoint policy";
    return core::Result<NetworkDecision>::FromValue(decision);
  }

  if (!AddressMatches(requirement.remote_address, endpoint.Value()->address_prefix)) {
    decision.reason = "remote address is outside endpoint allowlist";
    return core::Result<NetworkDecision>::FromValue(decision);
  }

  if (requirement.port < endpoint.Value()->port_min ||
      requirement.port > endpoint.Value()->port_max) {
    decision.reason = "remote port is outside endpoint allowlist";
    return core::Result<NetworkDecision>::FromValue(decision);
  }

  decision.allowed = true;
  decision.reason = "communication requirement is allowed";
  return core::Result<NetworkDecision>::FromValue(decision);
}

core::Result<FirewallPlan> NetworkManager::BuildFirewallPlan() const {
  FirewallPlan plan;
  plan.nftables_commands.push_back("nft add table inet openautosar");
  plan.nftables_commands.push_back(
    "nft add chain inet openautosar egress { type filter hook output priority 0 ; }");

  for (const auto& endpoint : endpoints_) {
    auto authorized = AuthorizeFirewallRule(endpoint);
    if (!authorized) {
      plan.rejected_rules.push_back(endpoint.endpoint_id + ": " + authorized.Error().message);
      continue;
    }

    plan.nftables_commands.push_back(RuleCommandFor(endpoint));
  }

  return core::Result<FirewallPlan>::FromValue(std::move(plan));
}

NetworkSnapshot NetworkManager::Snapshot() const {
  NetworkSnapshot snapshot;
  snapshot.endpoints = endpoints_;
  snapshot.interfaces.reserve(interfaces_.size());
  for (const auto& interface : interfaces_) {
    snapshot.interfaces.push_back(interface.status);
  }
  return snapshot;
}

void NetworkManager::SetFirewallAuthorization(
  const security::AccessPolicyEngine& access_policy,
  security::Principal principal) {
  firewall_authorization_ = &access_policy;
  firewall_principal_ = std::move(principal);
}

void NetworkManager::ClearFirewallAuthorization() noexcept {
  firewall_authorization_ = nullptr;
  firewall_principal_ = {};
}

void NetworkManager::SetSecurityEventCollector(
  security::SecurityEventCollector& collector) noexcept {
  security_events_ = &collector;
}

void NetworkManager::ClearSecurityEventCollector() noexcept {
  security_events_ = nullptr;
}

core::Result<NetworkManager::InterfaceRecord*> NetworkManager::FindInterfaceMutable(
  std::string_view interface_name) {
  auto iter = std::find_if(
    interfaces_.begin(),
    interfaces_.end(),
    [interface_name](const InterfaceRecord& record) {
      return record.policy.interface_name == interface_name;
    });
  if (iter == interfaces_.end()) {
    return core::Result<InterfaceRecord*>::FromError(
      MakeError("network interface is missing"));
  }

  return core::Result<InterfaceRecord*>::FromValue(&(*iter));
}

core::Result<const NetworkManager::InterfaceRecord*> NetworkManager::FindInterface(
  std::string_view interface_name) const {
  auto iter = std::find_if(
    interfaces_.begin(),
    interfaces_.end(),
    [interface_name](const InterfaceRecord& record) {
      return record.policy.interface_name == interface_name;
    });
  if (iter == interfaces_.end()) {
    return core::Result<const InterfaceRecord*>::FromError(
      MakeError("network interface is missing"));
  }

  return core::Result<const InterfaceRecord*>::FromValue(&(*iter));
}

core::Result<const EndpointPolicy*> NetworkManager::FindEndpoint(
  std::string_view endpoint_id) const {
  auto iter = std::find_if(
    endpoints_.begin(),
    endpoints_.end(),
    [endpoint_id](const EndpointPolicy& endpoint) {
      return endpoint.endpoint_id == endpoint_id;
    });
  if (iter == endpoints_.end()) {
    return core::Result<const EndpointPolicy*>::FromError(
      MakeError("endpoint policy is missing"));
  }

  return core::Result<const EndpointPolicy*>::FromValue(&(*iter));
}

core::Result<bool> NetworkManager::AuthorizeFirewallRule(
  const EndpointPolicy& endpoint) const {
  if (firewall_authorization_ == nullptr) {
    return core::Result<bool>::FromValue(true);
  }

  const auto decision = firewall_authorization_->Authorize({
    .principal = firewall_principal_,
    .resource = {
      .kind = security::ResourceKind::kFirewall,
      .identifier = endpoint.endpoint_id,
      .policy_id = endpoint.interface_name,
    },
    .operation = security::Operation::kInstallFirewallRule,
    .action = "install-firewall-rule",
  });
  if (!decision.Allowed()) {
    RecordFirewallDenial(endpoint, decision.reason);
    return core::Result<bool>::FromError(MakeError(decision.reason.c_str()));
  }

  return core::Result<bool>::FromValue(true);
}

void NetworkManager::RecordFirewallDenial(
  const EndpointPolicy& endpoint,
  std::string_view reason) const {
  if (security_events_ == nullptr) {
    return;
  }

  static_cast<void>(security_events_->Record({
    .source = "network-management",
    .category = "firewall",
    .severity = security::SecuritySeverity::kWarning,
    .principal_id = firewall_principal_.application_id,
    .resource_id = endpoint.endpoint_id,
    .operation = security::Operation::kInstallFirewallRule,
    .detail = std::string(reason),
  }));
}

std::string_view ToString(ClockQuality quality) noexcept {
  switch (quality) {
    case ClockQuality::kUnsynchronized:
      return "Unsynchronized";
    case ClockQuality::kSynchronized:
      return "Synchronized";
    case ClockQuality::kDegraded:
      return "Degraded";
    case ClockQuality::kLostSync:
      return "LostSync";
  }

  return "Unknown";
}

std::string_view ToString(TimeJumpPolicy policy) noexcept {
  switch (policy) {
    case TimeJumpPolicy::kReject:
      return "Reject";
    case TimeJumpPolicy::kDegrade:
      return "Degrade";
    case TimeJumpPolicy::kAccept:
      return "Accept";
  }

  return "Unknown";
}

std::string_view ToString(NetworkInterfaceState state) noexcept {
  switch (state) {
    case NetworkInterfaceState::kDown:
      return "Down";
    case NetworkInterfaceState::kReady:
      return "Ready";
    case NetworkInterfaceState::kDegraded:
      return "Degraded";
    case NetworkInterfaceState::kAsleep:
      return "Asleep";
  }

  return "Unknown";
}

std::string_view ToString(NetworkProtocol protocol) noexcept {
  switch (protocol) {
    case NetworkProtocol::kUdp:
      return "UDP";
    case NetworkProtocol::kTcp:
      return "TCP";
    case NetworkProtocol::kCan:
      return "CAN";
    case NetworkProtocol::kLocal:
      return "Local";
  }

  return "Unknown";
}

}  // namespace openautosar::runtime::tnm
