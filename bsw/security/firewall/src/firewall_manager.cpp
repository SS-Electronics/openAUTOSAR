// SPDX-License-Identifier: MIT

#include "openautosar/security/firewall_manager.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>
#include <utility>

namespace openautosar::security::firewall {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"firewall", message};
}

[[nodiscard]] bool IsSafeToken(std::string_view value) noexcept {
  if (value.empty()) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](unsigned char value_char) {
    return std::isalnum(value_char) != 0 || value_char == '_' || value_char == '-' ||
           value_char == '.' || value_char == ':' || value_char == '/';
  });
}

[[nodiscard]] bool IsSafeInterface(std::string_view value) noexcept {
  if (value.empty() || value.size() > 32U) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](unsigned char value_char) {
    return std::isalnum(value_char) != 0 || value_char == '_' || value_char == '-' ||
           value_char == '.';
  });
}

[[nodiscard]] bool ParseByte(std::string_view value, std::uint8_t& output) noexcept {
  if (value.empty() || value.size() > 3U) {
    return false;
  }

  unsigned int parsed{0U};
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end || parsed > 255U) {
    return false;
  }

  output = static_cast<std::uint8_t>(parsed);
  return true;
}

[[nodiscard]] bool ParsePrefix(std::string_view value, std::uint8_t& output) noexcept {
  if (value.empty() || value.size() > 2U) {
    return false;
  }

  unsigned int parsed{0U};
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end || parsed > 32U) {
    return false;
  }

  output = static_cast<std::uint8_t>(parsed);
  return true;
}

[[nodiscard]] bool IsValidIpv4Cidr(std::string_view value) noexcept {
  const auto slash = value.find('/');
  if (slash == std::string_view::npos || slash == 0U || slash + 1U >= value.size()) {
    return false;
  }

  std::uint8_t prefix{0U};
  if (!ParsePrefix(value.substr(slash + 1U), prefix)) {
    return false;
  }

  const auto address = value.substr(0U, slash);
  std::size_t begin{0U};
  for (std::size_t octet = 0U; octet < 4U; ++octet) {
    const auto dot = octet == 3U ? std::string_view::npos : address.find('.', begin);
    const auto end = dot == std::string_view::npos ? address.size() : dot;
    if (end <= begin) {
      return false;
    }

    std::uint8_t parsed{0U};
    if (!ParseByte(address.substr(begin, end - begin), parsed)) {
      return false;
    }

    if (octet < 3U && dot == std::string_view::npos) {
      return false;
    }
    begin = end + 1U;
  }

  return begin == address.size() + 1U;
}

[[nodiscard]] bool Contains(
  const std::vector<std::string>& values,
  std::string_view value) noexcept {
  return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool SupportsPorts(Protocol protocol) noexcept {
  return protocol == Protocol::kTcp || protocol == Protocol::kUdp;
}

[[nodiscard]] std::string ActionText(RuleAction action, EnforcementBackend backend) {
  if (backend != EnforcementBackend::kNftables) {
    return std::string(ToString(action));
  }

  switch (action) {
    case RuleAction::kAllow:
      return "accept";
    case RuleAction::kDeny:
      return "drop";
    case RuleAction::kReject:
      return "reject";
  }

  return "drop";
}

[[nodiscard]] const char* ChainText(Direction direction) noexcept {
  return direction == Direction::kIngress ? "ingress" : "egress";
}

[[nodiscard]] core::Result<bool> ValidateRule(
  const FirewallRule& rule,
  const FirewallPolicy& policy) {
  if (!IsSafeToken(rule.rule_id)) {
    return core::Result<bool>::FromError(MakeError("firewall rule id is invalid"));
  }

  if (!IsSafeInterface(rule.interface_name)) {
    return core::Result<bool>::FromError(MakeError("firewall interface name is invalid"));
  }

  if (!policy.allowed_interfaces.empty() &&
      !Contains(policy.allowed_interfaces, rule.interface_name)) {
    return core::Result<bool>::FromError(MakeError("firewall interface is not allowlisted"));
  }

  if (!IsValidIpv4Cidr(rule.source.cidr) || !IsValidIpv4Cidr(rule.destination.cidr)) {
    return core::Result<bool>::FromError(MakeError("firewall rule CIDR is invalid"));
  }

  if ((rule.source.port.has_value() || rule.destination.port.has_value()) &&
      !SupportsPorts(rule.protocol)) {
    return core::Result<bool>::FromError(
      MakeError("firewall rule ports require TCP or UDP"));
  }

  if (!rule.service_instance.empty() && !IsSafeToken(rule.service_instance)) {
    return core::Result<bool>::FromError(
      MakeError("firewall service instance token is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] std::string EndpointExpression(
  const EndpointSelector& endpoint,
  bool source,
  Protocol protocol,
  EnforcementBackend backend) {
  std::ostringstream output;
  const auto side = source ? std::string_view("src") : std::string_view("dst");
  if (backend == EnforcementBackend::kNftables) {
    output << " ip " << side << "addr " << endpoint.cidr;
    if (endpoint.port.has_value()) {
      output << ' ' << ToString(protocol) << ' ';
      output << (source ? "sport " : "dport ") << endpoint.port.value();
    }
  } else {
    output << ' ' << side << '=' << endpoint.cidr;
    output << ' ' << side << "_port=";
    output << (endpoint.port.has_value() ? std::to_string(endpoint.port.value()) : "*");
  }

  return output.str();
}

[[nodiscard]] std::string CompileExpression(
  const FirewallRule& rule,
  EnforcementBackend backend) {
  std::ostringstream output;
  if (backend == EnforcementBackend::kNftables) {
    output << "add rule inet openautosar " << ChainText(rule.direction) << ' ';
    output << (rule.direction == Direction::kIngress ? "iifname" : "oifname");
    output << " \"" << rule.interface_name << "\"";
    if (rule.protocol != Protocol::kAny) {
      output << ' ' << ToString(rule.protocol);
    }
    output << EndpointExpression(rule.source, true, rule.protocol, backend);
    output << EndpointExpression(rule.destination, false, rule.protocol, backend);
    output << ' ' << ActionText(rule.action, backend);
    output << " comment \"" << rule.rule_id << "\"";
    return output.str();
  }

  output << ToString(backend) << " direction=" << ToString(rule.direction);
  output << " iface=" << rule.interface_name << " proto=" << ToString(rule.protocol);
  output << EndpointExpression(rule.source, true, rule.protocol, backend);
  output << EndpointExpression(rule.destination, false, rule.protocol, backend);
  output << " action=" << ActionText(rule.action, backend);
  if (!rule.service_instance.empty()) {
    output << " service=" << rule.service_instance;
  }
  output << " id=" << rule.rule_id;
  return output.str();
}

[[nodiscard]] FirewallRule LoopbackRule(Direction direction) {
  return {
    .rule_id = direction == Direction::kIngress ? "openautosar-loopback-in"
                                                : "openautosar-loopback-out",
    .direction = direction,
    .action = RuleAction::kAllow,
    .protocol = Protocol::kAny,
    .interface_name = "lo",
    .source = {.cidr = "127.0.0.0/8", .port = std::nullopt},
    .destination = {.cidr = "127.0.0.0/8", .port = std::nullopt},
    .service_instance = "platform-loopback",
    .priority = 0U,
    .enabled = true,
    .audit = false,
  };
}

[[nodiscard]] FirewallRule DefaultRule(Direction direction, RuleAction action) {
  return {
    .rule_id = direction == Direction::kIngress ? "openautosar-default-ingress"
                                                : "openautosar-default-egress",
    .direction = direction,
    .action = action,
    .protocol = Protocol::kAny,
    .interface_name = direction == Direction::kIngress ? "tap-openautosar0" : "tap-openautosar0",
    .source = {.cidr = "0.0.0.0/0", .port = std::nullopt},
    .destination = {.cidr = "0.0.0.0/0", .port = std::nullopt},
    .service_instance = "platform-default",
    .priority = 4'294'967'295U,
    .enabled = true,
    .audit = true,
  };
}

[[nodiscard]] CompiledFirewallRule CompileRule(
  const FirewallRule& rule,
  EnforcementBackend backend) {
  return {
    .rule_id = rule.rule_id,
    .backend = backend,
    .direction = rule.direction,
    .action = rule.action,
    .protocol = rule.protocol,
    .expression = CompileExpression(rule, backend),
    .audit_key = rule.audit ? "firewall:" + rule.rule_id : "",
  };
}

}  // namespace

core::Result<FirewallPlan> FirewallManager::Compile(FirewallPolicy policy) const {
  if (policy.max_rules == 0U) {
    return core::Result<FirewallPlan>::FromError(MakeError("firewall rule budget is zero"));
  }

  const bool default_deny =
    policy.default_ingress != RuleAction::kAllow && policy.default_egress != RuleAction::kAllow;
  if (policy.require_default_deny && !default_deny) {
    return core::Result<FirewallPlan>::FromError(
      MakeError("firewall policy must enforce default deny"));
  }

  std::vector<FirewallRule> enabled_rules;
  std::size_t disabled_count{0U};
  for (auto& rule : policy.rules) {
    if (rule.enabled) {
      enabled_rules.push_back(std::move(rule));
    } else {
      ++disabled_count;
    }
  }

  if (enabled_rules.size() > policy.max_rules) {
    return core::Result<FirewallPlan>::FromError(MakeError("firewall rule budget exceeded"));
  }

  if (policy.allow_loopback) {
    enabled_rules.push_back(LoopbackRule(Direction::kIngress));
    enabled_rules.push_back(LoopbackRule(Direction::kEgress));
  }
  enabled_rules.push_back(DefaultRule(Direction::kIngress, policy.default_ingress));
  enabled_rules.push_back(DefaultRule(Direction::kEgress, policy.default_egress));

  std::sort(enabled_rules.begin(), enabled_rules.end(), [](const auto& left, const auto& right) {
    if (left.priority == right.priority) {
      return left.rule_id < right.rule_id;
    }
    return left.priority < right.priority;
  });

  FirewallPlan plan{
    .rules = {},
    .disabled_rule_count = disabled_count,
    .default_deny_enforced = default_deny,
  };
  std::vector<std::string> seen_ids;
  for (const auto& rule : enabled_rules) {
    if (Contains(seen_ids, rule.rule_id)) {
      return core::Result<FirewallPlan>::FromError(MakeError("firewall rule id is duplicated"));
    }

    auto valid = ValidateRule(rule, policy);
    if (!valid) {
      return core::Result<FirewallPlan>::FromError(valid.Error());
    }

    seen_ids.push_back(rule.rule_id);
    plan.rules.push_back(CompileRule(rule, policy.backend));
  }

  return core::Result<FirewallPlan>::FromValue(std::move(plan));
}

core::Result<FirewallInstallReceipt> FirewallManager::InstallPlan(
  const Principal& principal,
  const AccessPolicyEngine& access_policy,
  FirewallPlan plan) {
  if (plan.rules.empty()) {
    return core::Result<FirewallInstallReceipt>::FromError(
      MakeError("firewall plan is empty"));
  }

  const auto decision = access_policy.Authorize({
    .principal = principal,
    .resource = {
      .kind = ResourceKind::kFirewall,
      .identifier = "openautosar-firewall",
      .policy_id = "openautosar-firewall",
    },
    .operation = Operation::kInstallFirewallRule,
    .action = "install-firewall-plan",
  });
  if (!decision.Allowed()) {
    return core::Result<FirewallInstallReceipt>::FromError({"firewall", decision.reason});
  }

  const auto installed_rule_count = plan.rules.size();
  active_plan_ = std::move(plan);
  return core::Result<FirewallInstallReceipt>::FromValue({
    .installed = true,
    .reason = "firewall plan installed in staged model",
    .installed_rule_count = installed_rule_count,
  });
}

std::string_view ToString(Direction direction) noexcept {
  switch (direction) {
    case Direction::kIngress:
      return "ingress";
    case Direction::kEgress:
      return "egress";
  }

  return "unknown";
}

std::string_view ToString(Protocol protocol) noexcept {
  switch (protocol) {
    case Protocol::kAny:
      return "any";
    case Protocol::kTcp:
      return "tcp";
    case Protocol::kUdp:
      return "udp";
    case Protocol::kIcmp:
      return "icmp";
  }

  return "unknown";
}

std::string_view ToString(RuleAction action) noexcept {
  switch (action) {
    case RuleAction::kAllow:
      return "allow";
    case RuleAction::kDeny:
      return "deny";
    case RuleAction::kReject:
      return "reject";
  }

  return "unknown";
}

std::string_view ToString(EnforcementBackend backend) noexcept {
  switch (backend) {
    case EnforcementBackend::kNftables:
      return "nftables";
    case EnforcementBackend::kEbpf:
      return "ebpf";
    case EnforcementBackend::kApplicationPolicy:
      return "application-policy";
  }

  return "unknown";
}

}  // namespace openautosar::security::firewall
