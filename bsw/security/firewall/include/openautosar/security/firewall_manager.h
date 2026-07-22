// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/security/identity_access_manager.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::security::firewall {

enum class Direction {
  kIngress,
  kEgress,
};

enum class Protocol {
  kAny,
  kTcp,
  kUdp,
  kIcmp,
};

enum class RuleAction {
  kAllow,
  kDeny,
  kReject,
};

enum class EnforcementBackend {
  kNftables,
  kEbpf,
  kApplicationPolicy,
};

struct EndpointSelector final {
  std::string cidr{"0.0.0.0/0"};
  std::optional<std::uint16_t> port;
};

struct FirewallRule final {
  std::string rule_id;
  Direction direction{Direction::kIngress};
  RuleAction action{RuleAction::kDeny};
  Protocol protocol{Protocol::kAny};
  std::string interface_name;
  EndpointSelector source;
  EndpointSelector destination;
  std::string service_instance;
  std::uint32_t priority{100U};
  bool enabled{true};
  bool audit{true};
};

struct FirewallPolicy final {
  EnforcementBackend backend{EnforcementBackend::kNftables};
  RuleAction default_ingress{RuleAction::kDeny};
  RuleAction default_egress{RuleAction::kDeny};
  bool allow_loopback{true};
  bool require_default_deny{true};
  std::size_t max_rules{128U};
  std::vector<std::string> allowed_interfaces;
  std::vector<FirewallRule> rules;
};

struct CompiledFirewallRule final {
  std::string rule_id;
  EnforcementBackend backend{EnforcementBackend::kNftables};
  Direction direction{Direction::kIngress};
  RuleAction action{RuleAction::kDeny};
  Protocol protocol{Protocol::kAny};
  std::string expression;
  std::string audit_key;
};

struct FirewallPlan final {
  std::vector<CompiledFirewallRule> rules;
  std::size_t disabled_rule_count{0U};
  bool default_deny_enforced{false};
};

struct FirewallInstallReceipt final {
  bool installed{false};
  std::string reason;
  std::size_t installed_rule_count{0U};
};

class FirewallManager final {
public:
  FirewallManager() = default;

  [[nodiscard]] core::Result<FirewallPlan> Compile(FirewallPolicy policy) const;
  [[nodiscard]] core::Result<FirewallInstallReceipt> InstallPlan(
    const Principal& principal,
    const AccessPolicyEngine& access_policy,
    FirewallPlan plan);

  [[nodiscard]] const std::optional<FirewallPlan>& ActivePlan() const noexcept {
    return active_plan_;
  }

private:
  std::optional<FirewallPlan> active_plan_;
};

[[nodiscard]] std::string_view ToString(Direction direction) noexcept;
[[nodiscard]] std::string_view ToString(Protocol protocol) noexcept;
[[nodiscard]] std::string_view ToString(RuleAction action) noexcept;
[[nodiscard]] std::string_view ToString(EnforcementBackend backend) noexcept;

}  // namespace openautosar::security::firewall
