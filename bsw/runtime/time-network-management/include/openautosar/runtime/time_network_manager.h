// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/runtime/execution_manager.h"
#include "openautosar/security/identity_access_manager.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::runtime::tnm {

enum class ClockQuality {
  kUnsynchronized,
  kSynchronized,
  kDegraded,
  kLostSync,
};

enum class TimeJumpPolicy {
  kReject,
  kDegrade,
  kAccept,
};

enum class NetworkInterfaceState {
  kDown,
  kReady,
  kDegraded,
  kAsleep,
};

enum class NetworkProtocol {
  kUdp,
  kTcp,
  kCan,
  kLocal,
};

struct ClockDomainConfig final {
  std::string domain_id;
  std::uint64_t max_uncertainty_ns{1'000'000U};
  std::uint64_t jump_threshold_ns{5'000'000U};
  std::uint64_t loss_of_sync_timeout_ns{1'000'000'000U};
  TimeJumpPolicy jump_policy{TimeJumpPolicy::kDegrade};
};

struct TimeSyncSample final {
  std::string domain_id;
  std::uint64_t monotonic_ns{0U};
  std::int64_t global_time_ns{0};
  std::uint64_t uncertainty_ns{0U};
  std::string source;
};

struct TimeSnapshot final {
  std::string domain_id;
  std::uint64_t monotonic_ns{0U};
  std::int64_t global_time_ns{0};
  std::uint64_t uncertainty_ns{0U};
  ClockQuality quality{ClockQuality::kUnsynchronized};
  bool synchronized{false};
  std::string source;
};

class VirtualClock final {
public:
  [[nodiscard]] std::uint64_t NowNs() const noexcept { return now_ns_; }
  [[nodiscard]] core::Result<std::uint64_t> AdvanceBy(std::uint64_t delta_ns);
  [[nodiscard]] core::Result<std::uint64_t> AdvanceTo(std::uint64_t next_ns);
  void Reset(std::uint64_t now_ns = 0U) noexcept { now_ns_ = now_ns; }

private:
  std::uint64_t now_ns_{0U};
};

class TimeManager final {
public:
  [[nodiscard]] core::Result<bool> RegisterClockDomain(ClockDomainConfig config);
  [[nodiscard]] core::Result<TimeSnapshot> ObserveTimeSync(TimeSyncSample sample);
  [[nodiscard]] core::Result<TimeSnapshot> EvaluateLossOfSync(
    std::string_view domain_id,
    std::uint64_t monotonic_now_ns);
  [[nodiscard]] core::Result<TimeSnapshot> Snapshot(std::string_view domain_id) const;
  [[nodiscard]] std::vector<TimeSnapshot> Snapshots() const;

private:
  struct ClockDomainRecord final {
    ClockDomainConfig config;
    TimeSnapshot snapshot;
    bool has_sample{false};
  };

  [[nodiscard]] core::Result<ClockDomainRecord*> FindMutable(std::string_view domain_id);
  [[nodiscard]] core::Result<const ClockDomainRecord*> Find(
    std::string_view domain_id) const;

  std::vector<ClockDomainRecord> domains_;
};

struct NetworkInterfacePolicy final {
  std::string interface_name;
  bool required{true};
  bool allow_wakeup{true};
  bool allow_sleep{true};
  std::uint64_t recovery_timeout_ms{1'000U};
  std::vector<FunctionGroupState> function_groups;
};

struct NetworkInterfaceStatus final {
  std::string interface_name;
  NetworkInterfaceState state{NetworkInterfaceState::kDown};
  std::uint64_t timestamp_ms{0U};
  std::string detail;
};

struct EndpointPolicy final {
  std::string endpoint_id;
  std::string interface_name;
  NetworkProtocol protocol{NetworkProtocol::kUdp};
  std::string address_prefix;
  std::uint16_t port_min{0U};
  std::uint16_t port_max{0U};
  bool allow_in_update_mode{false};
  std::vector<FunctionGroupState> function_groups;
};

struct CommunicationRequirement final {
  std::string application_id;
  std::string endpoint_id;
  NetworkProtocol protocol{NetworkProtocol::kUdp};
  std::string remote_address;
  std::uint16_t port{0U};
  FunctionGroupState function_group{FunctionGroupState::kStartup};
  bool update_mode{false};
};

struct NetworkDecision final {
  bool allowed{false};
  std::string reason;
  std::string interface_name;
  NetworkInterfaceState interface_state{NetworkInterfaceState::kDown};
};

struct FirewallPlan final {
  std::vector<std::string> nftables_commands;
  std::vector<std::string> rejected_rules;
};

struct NetworkSnapshot final {
  std::vector<NetworkInterfaceStatus> interfaces;
  std::vector<EndpointPolicy> endpoints;
};

class NetworkManager final {
public:
  [[nodiscard]] core::Result<bool> RegisterInterface(NetworkInterfacePolicy policy);
  [[nodiscard]] core::Result<bool> UpdateInterfaceState(NetworkInterfaceStatus status);
  [[nodiscard]] core::Result<bool> RegisterEndpointPolicy(EndpointPolicy policy);

  [[nodiscard]] core::Result<NetworkDecision> Evaluate(
    const CommunicationRequirement& requirement) const;
  [[nodiscard]] core::Result<FirewallPlan> BuildFirewallPlan() const;
  [[nodiscard]] NetworkSnapshot Snapshot() const;

  void SetFirewallAuthorization(
    const security::AccessPolicyEngine& access_policy,
    security::Principal principal);
  void ClearFirewallAuthorization() noexcept;
  void SetSecurityEventCollector(security::SecurityEventCollector& collector) noexcept;
  void ClearSecurityEventCollector() noexcept;

private:
  struct InterfaceRecord final {
    NetworkInterfacePolicy policy;
    NetworkInterfaceStatus status;
  };

  [[nodiscard]] core::Result<InterfaceRecord*> FindInterfaceMutable(
    std::string_view interface_name);
  [[nodiscard]] core::Result<const InterfaceRecord*> FindInterface(
    std::string_view interface_name) const;
  [[nodiscard]] core::Result<const EndpointPolicy*> FindEndpoint(
    std::string_view endpoint_id) const;
  [[nodiscard]] core::Result<bool> AuthorizeFirewallRule(
    const EndpointPolicy& endpoint) const;
  void RecordFirewallDenial(const EndpointPolicy& endpoint, std::string_view reason) const;

  std::vector<InterfaceRecord> interfaces_;
  std::vector<EndpointPolicy> endpoints_;
  const security::AccessPolicyEngine* firewall_authorization_{nullptr};
  security::SecurityEventCollector* security_events_{nullptr};
  security::Principal firewall_principal_{};
};

[[nodiscard]] std::string_view ToString(ClockQuality quality) noexcept;
[[nodiscard]] std::string_view ToString(TimeJumpPolicy policy) noexcept;
[[nodiscard]] std::string_view ToString(NetworkInterfaceState state) noexcept;
[[nodiscard]] std::string_view ToString(NetworkProtocol protocol) noexcept;

}  // namespace openautosar::runtime::tnm
