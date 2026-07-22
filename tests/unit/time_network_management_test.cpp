// SPDX-License-Identifier: MIT

#include "openautosar/runtime/time_network_manager.h"

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

openautosar::security::Principal Principal(
  std::string_view application,
  std::vector<std::string> roles) {
  openautosar::security::Principal principal;
  principal.application_id = application;
  principal.machine_id = "qemux86-64";
  principal.security_label = "openautosar.network";
  principal.roles = std::move(roles);
  principal.authenticated = true;
  return principal;
}

bool Contains(std::string_view value, std::string_view expected) {
  return value.find(expected) != std::string_view::npos;
}

}  // namespace

int main() {
  namespace iam = openautosar::security;
  namespace runtime = openautosar::runtime;
  namespace tnm = openautosar::runtime::tnm;

  tnm::VirtualClock clock;
  Require(clock.AdvanceBy(10U).Value() == 10U, "virtual clock did not advance");
  Require(!clock.AdvanceTo(9U).HasValue(), "virtual clock moved backward");
  Require(clock.AdvanceTo(20U).Value() == 20U, "virtual clock did not advance to target");

  tnm::TimeManager time;
  Require(!time.RegisterClockDomain({}).HasValue(), "invalid clock domain was accepted");
  Require(time.RegisterClockDomain({
            .domain_id = "gptp",
            .max_uncertainty_ns = 500U,
            .jump_threshold_ns = 1'000U,
            .loss_of_sync_timeout_ns = 10'000U,
            .jump_policy = tnm::TimeJumpPolicy::kDegrade,
          }).HasValue(),
          "gPTP clock domain was rejected");

  auto synchronized = time.ObserveTimeSync({
    .domain_id = "gptp",
    .monotonic_ns = 1'000U,
    .global_time_ns = 100'000,
    .uncertainty_ns = 100U,
    .source = "virtual-gptp",
  });
  Require(synchronized.HasValue(), "valid time sync sample was rejected");
  Require(
    synchronized.Value().quality == tnm::ClockQuality::kSynchronized,
    "valid time sync was not synchronized");

  auto degraded = time.ObserveTimeSync({
    .domain_id = "gptp",
    .monotonic_ns = 2'000U,
    .global_time_ns = 106'000,
    .uncertainty_ns = 700U,
    .source = "virtual-gptp",
  });
  Require(degraded.HasValue(), "degraded time sync sample was rejected");
  Require(
    degraded.Value().quality == tnm::ClockQuality::kDegraded,
    "uncertainty or jump did not degrade clock quality");

  auto lost = time.EvaluateLossOfSync("gptp", 20'001U);
  Require(lost.HasValue(), "loss-of-sync evaluation failed");
  Require(lost.Value().quality == tnm::ClockQuality::kLostSync, "loss-of-sync missing");
  Require(!lost.Value().synchronized, "lost-sync clock remained synchronized");

  tnm::TimeManager reject_time;
  Require(reject_time.RegisterClockDomain({
            .domain_id = "strict",
            .max_uncertainty_ns = 500U,
            .jump_threshold_ns = 100U,
            .loss_of_sync_timeout_ns = 10'000U,
            .jump_policy = tnm::TimeJumpPolicy::kReject,
          }).HasValue(),
          "strict clock domain was rejected");
  Require(reject_time.ObserveTimeSync({
            .domain_id = "strict",
            .monotonic_ns = 0U,
            .global_time_ns = 1'000,
            .uncertainty_ns = 10U,
            .source = "virtual-gptp",
          }).HasValue(),
          "strict first time sample failed");
  Require(!reject_time.ObserveTimeSync({
             .domain_id = "strict",
             .monotonic_ns = 1'000U,
             .global_time_ns = 5'000,
             .uncertainty_ns = 10U,
             .source = "virtual-gptp",
           }).HasValue(),
          "strict clock accepted a rejected time jump");

  tnm::NetworkManager network;
  Require(network.RegisterInterface({
            .interface_name = "tap-openautosar0",
            .required = true,
            .function_groups = {
              runtime::FunctionGroupState::kStartup,
              runtime::FunctionGroupState::kDrivingReady,
              runtime::FunctionGroupState::kDegraded,
            },
          }).HasValue(),
          "network interface policy was rejected");
  Require(network.RegisterEndpointPolicy({
            .endpoint_id = "ultrasonic-someip",
            .interface_name = "tap-openautosar0",
            .protocol = tnm::NetworkProtocol::kUdp,
            .address_prefix = "10.42.0.",
            .port_min = 30'500U,
            .port_max = 30'599U,
            .allow_in_update_mode = false,
            .function_groups = {runtime::FunctionGroupState::kDrivingReady},
          }).HasValue(),
          "SOME/IP endpoint policy was rejected");

  auto not_ready = network.Evaluate({
    .application_id = "oa-ultrasonic-gateway-smoke",
    .endpoint_id = "ultrasonic-someip",
    .protocol = tnm::NetworkProtocol::kUdp,
    .remote_address = "10.42.0.30",
    .port = 30'501U,
    .function_group = runtime::FunctionGroupState::kDrivingReady,
    .update_mode = false,
  });
  Require(not_ready.HasValue(), "not-ready network decision failed");
  Require(!not_ready.Value().allowed, "network allowed traffic while interface was down");

  Require(network.UpdateInterfaceState({
            .interface_name = "tap-openautosar0",
            .state = tnm::NetworkInterfaceState::kReady,
            .timestamp_ms = 10U,
            .detail = "QEMU TAP ready",
          }).HasValue(),
          "network interface readiness update failed");

  auto allowed = network.Evaluate({
    .application_id = "oa-ultrasonic-gateway-smoke",
    .endpoint_id = "ultrasonic-someip",
    .protocol = tnm::NetworkProtocol::kUdp,
    .remote_address = "10.42.0.30",
    .port = 30'501U,
    .function_group = runtime::FunctionGroupState::kDrivingReady,
    .update_mode = false,
  });
  Require(allowed.HasValue() && allowed.Value().allowed, "valid network traffic was denied");

  auto update_denied = network.Evaluate({
    .application_id = "oa-ucm",
    .endpoint_id = "ultrasonic-someip",
    .protocol = tnm::NetworkProtocol::kUdp,
    .remote_address = "10.42.0.30",
    .port = 30'501U,
    .function_group = runtime::FunctionGroupState::kDrivingReady,
    .update_mode = true,
  });
  Require(update_denied.HasValue(), "update-mode network decision failed");
  Require(!update_denied.Value().allowed, "update-mode policy was ignored");

  auto wrong_address = network.Evaluate({
    .application_id = "oa-ultrasonic-gateway-smoke",
    .endpoint_id = "ultrasonic-someip",
    .protocol = tnm::NetworkProtocol::kUdp,
    .remote_address = "192.0.2.10",
    .port = 30'501U,
    .function_group = runtime::FunctionGroupState::kDrivingReady,
    .update_mode = false,
  });
  Require(wrong_address.HasValue(), "wrong-address network decision failed");
  Require(!wrong_address.Value().allowed, "address allowlist was ignored");

  auto unauthenticated_plan = network.BuildFirewallPlan();
  Require(unauthenticated_plan.HasValue(), "firewall plan generation failed");
  Require(
    Contains(unauthenticated_plan.Value().nftables_commands.back(), "ultrasonic-someip"),
    "firewall plan did not include SOME/IP endpoint");

  iam::SecurityPolicy firewall_policy;
  firewall_policy.production_mode = true;
  firewall_policy.default_decision = iam::Decision::kDeny;
  iam::AccessRule firewall_rule;
  firewall_rule.decision = iam::Decision::kAllow;
  firewall_rule.operations = {iam::Operation::kInstallFirewallRule};
  firewall_rule.resource_kinds = {iam::ResourceKind::kFirewall};
  firewall_rule.roles = {"network-admin"};
  firewall_rule.require_authenticated = true;
  firewall_rule.allow_remote = false;
  firewall_rule.reason = "network administrator rule";
  firewall_policy.rules.push_back(std::move(firewall_rule));
  iam::AccessPolicyEngine firewall_access{firewall_policy};
  iam::SecurityEventCollector events{{.max_events = 8U, .aggregation_window_ms = 100U}};
  network.SetSecurityEventCollector(events);
  network.SetFirewallAuthorization(firewall_access, Principal("oa-dashboard", {"viewer"}));

  auto rejected_plan = network.BuildFirewallPlan();
  Require(rejected_plan.HasValue(), "rejected firewall plan generation failed");
  Require(!rejected_plan.Value().rejected_rules.empty(), "unauthorized firewall rule installed");
  Require(
    events.CountBySeverity(iam::SecuritySeverity::kWarning) == 1U,
    "firewall authorization denial was not reported");

  network.SetFirewallAuthorization(
    firewall_access,
    Principal("openautosar-network-manager", {"network-admin"}));
  auto authorized_plan = network.BuildFirewallPlan();
  Require(authorized_plan.HasValue(), "authorized firewall plan generation failed");
  Require(authorized_plan.Value().rejected_rules.empty(), "authorized firewall rule rejected");

  Require(tnm::ToString(tnm::ClockQuality::kLostSync) == std::string_view("LostSync"),
          "clock quality text changed");
  Require(tnm::ToString(tnm::NetworkProtocol::kUdp) == std::string_view("UDP"),
          "network protocol text changed");

  return 0;
}
