// SPDX-License-Identifier: MIT

#include "openautosar/security/firewall_manager.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

openautosar::security::firewall::FirewallRule SomeipOfferRule() {
  namespace fw = openautosar::security::firewall;
  return {
    .rule_id = "ultrasonic-someip-sd",
    .direction = fw::Direction::kIngress,
    .action = fw::RuleAction::kAllow,
    .protocol = fw::Protocol::kUdp,
    .interface_name = "tap-openautosar0",
    .source = {.cidr = "10.10.0.0/24", .port = std::nullopt},
    .destination = {.cidr = "10.10.0.2/32", .port = 30490U},
    .service_instance = "ultrasonic-service",
    .priority = 10U,
    .enabled = true,
    .audit = true,
  };
}

openautosar::security::firewall::FirewallPolicy Policy() {
  namespace fw = openautosar::security::firewall;
  fw::FirewallPolicy policy;
  policy.backend = fw::EnforcementBackend::kNftables;
  policy.default_ingress = fw::RuleAction::kDeny;
  policy.default_egress = fw::RuleAction::kDeny;
  policy.allow_loopback = true;
  policy.require_default_deny = true;
  policy.max_rules = 4U;
  policy.allowed_interfaces = {"tap-openautosar0", "lo"};
  policy.rules.push_back(SomeipOfferRule());
  policy.rules.push_back({
    .rule_id = "disabled-debug-ssh",
    .direction = fw::Direction::kIngress,
    .action = fw::RuleAction::kAllow,
    .protocol = fw::Protocol::kTcp,
    .interface_name = "tap-openautosar0",
    .source = {.cidr = "10.10.0.0/24", .port = std::nullopt},
    .destination = {.cidr = "10.10.0.2/32", .port = 22U},
    .service_instance = "debug-ssh",
    .priority = 20U,
    .enabled = false,
    .audit = true,
  });
  return policy;
}

openautosar::security::Principal Principal(
  std::string_view application,
  std::vector<std::string> roles) {
  openautosar::security::Principal principal;
  principal.application_id = application;
  principal.machine_id = "qemux86-64";
  principal.security_label = "openautosar.firewall.test";
  principal.roles = std::move(roles);
  principal.authenticated = true;
  principal.remote = false;
  return principal;
}

openautosar::security::AccessPolicyEngine AccessPolicy() {
  namespace iam = openautosar::security;
  iam::SecurityPolicy policy;
  policy.production_mode = true;
  policy.default_decision = iam::Decision::kDeny;
  iam::AccessRule rule;
  rule.decision = iam::Decision::kAllow;
  rule.operations = {iam::Operation::kInstallFirewallRule};
  rule.resource_kinds = {iam::ResourceKind::kFirewall};
  rule.resource_prefixes = {"openautosar-firewall"};
  rule.roles = {"network-policy-admin"};
  rule.require_authenticated = true;
  rule.allow_remote = false;
  rule.reason = "firewall admin";
  policy.rules.push_back(rule);
  return iam::AccessPolicyEngine{policy};
}

}  // namespace

int main() {
  namespace fw = openautosar::security::firewall;

  fw::FirewallManager manager;
  auto plan = manager.Compile(Policy());
  Require(plan.HasValue(), "valid firewall policy did not compile");
  Require(plan.Value().default_deny_enforced, "default deny was not enforced");
  Require(plan.Value().disabled_rule_count == 1U, "disabled rule count changed");
  Require(plan.Value().rules.size() == 5U, "compiled rule count changed");
  Require(
    plan.Value().rules[0U].rule_id == "openautosar-loopback-in",
    "firewall rules are not priority ordered");
  Require(
    plan.Value().rules[2U].expression.find("udp dport 30490") != std::string::npos,
    "SOME/IP-SD UDP destination port was not compiled");
  Require(
    plan.Value().rules[4U].rule_id == "openautosar-default-ingress",
    "default ingress rule ordering changed");

  auto weak_policy = Policy();
  weak_policy.default_ingress = fw::RuleAction::kAllow;
  Require(!manager.Compile(weak_policy).HasValue(), "default-allow policy was accepted");

  auto malformed = Policy();
  malformed.rules[0U].source.cidr = "10.10.0.0/33";
  Require(!manager.Compile(malformed).HasValue(), "malformed CIDR was accepted");

  auto unsafe_interface = Policy();
  unsafe_interface.rules[0U].interface_name = "tap openautosar";
  Require(!manager.Compile(unsafe_interface).HasValue(), "unsafe interface was accepted");

  auto unsupported_port = Policy();
  unsupported_port.rules[0U].protocol = fw::Protocol::kIcmp;
  Require(!manager.Compile(unsupported_port).HasValue(), "ICMP port selector was accepted");

  auto duplicate = Policy();
  duplicate.rules.push_back(SomeipOfferRule());
  Require(!manager.Compile(duplicate).HasValue(), "duplicate firewall rule id was accepted");

  auto ebpf_policy = Policy();
  ebpf_policy.backend = fw::EnforcementBackend::kEbpf;
  auto ebpf_plan = manager.Compile(ebpf_policy);
  Require(ebpf_plan.HasValue(), "eBPF firewall plan did not compile");
  Require(
    ebpf_plan.Value().rules[2U].expression.find("ebpf direction=ingress") == 0U,
    "eBPF expression format changed");

  auto access = AccessPolicy();
  auto denied_plan = manager.Compile(Policy());
  Require(denied_plan.HasValue(), "denied install plan did not compile");
  auto denied = manager.InstallPlan(
    Principal("oa-dashboard", {"ultrasonic-consumer"}),
    access,
    std::move(denied_plan.Value()));
  Require(!denied.HasValue(), "non-admin firewall install was allowed");
  Require(!manager.ActivePlan().has_value(), "denied firewall install changed active plan");

  auto allowed_plan = manager.Compile(Policy());
  Require(allowed_plan.HasValue(), "allowed install plan did not compile");
  auto installed = manager.InstallPlan(
    Principal("oa-network-policy", {"network-policy-admin"}),
    access,
    std::move(allowed_plan.Value()));
  Require(installed.HasValue(), "admin firewall install was denied");
  Require(installed.Value().installed, "firewall install receipt changed");
  Require(
    installed.Value().installed_rule_count == manager.ActivePlan()->rules.size(),
    "installed rule count did not match active plan");

  Require(fw::ToString(fw::RuleAction::kReject) == std::string_view("reject"),
          "firewall action text changed");
  Require(fw::ToString(fw::EnforcementBackend::kApplicationPolicy) ==
            std::string_view("application-policy"),
          "firewall backend text changed");

  return 0;
}
