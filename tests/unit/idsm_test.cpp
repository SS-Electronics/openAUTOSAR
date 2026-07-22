// SPDX-License-Identifier: MIT

#include "openautosar/security/idsm_manager.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

openautosar::security::idsm::IdsmPolicy Policy() {
  return {
    .max_records = 2U,
    .aggregation_window_ms = 100U,
    .rate_limit_count = 2U,
    .rate_limit_window_ms = 100U,
    .forwarding_threshold = openautosar::security::SecuritySeverity::kWarning,
    .backend_forwarding_enabled = true,
    .privacy_mode = openautosar::security::idsm::PrivacyMode::kRedactIdentifiers,
  };
}

openautosar::security::idsm::IdsmEvent FirewallEvent(std::uint64_t timestamp_ms) {
  namespace idsm = openautosar::security::idsm;
  return {
    .source = idsm::EventSource::kFirewall,
    .category = "authorization",
    .severity = openautosar::security::SecuritySeverity::kCritical,
    .principal_id = "root-shell",
    .resource_id = "ultrasonic-someip-sd",
    .operation = openautosar::security::Operation::kInstallFirewallRule,
    .detail = "firewall install denied",
    .count = 1U,
    .timestamp_ms = timestamp_ms,
  };
}

}  // namespace

int main() {
  namespace iam = openautosar::security;
  namespace idsm = openautosar::security::idsm;

  idsm::IdsmManager manager{Policy()};
  auto first = manager.Record(FirewallEvent(10U));
  Require(first.HasValue(), "first IDSM event was rejected");
  Require(first.Value().action == idsm::IdsmAction::kAccepted, "first action changed");
  Require(first.Value().forwarded, "critical firewall event was not forwarded");

  auto snapshot = manager.Snapshot();
  Require(snapshot.records.size() == 1U, "first IDSM snapshot size changed");
  Require(snapshot.records[0U].principal_id == "redacted", "principal was not redacted");
  Require(snapshot.records[0U].resource_id == "redacted", "resource was not redacted");
  Require(snapshot.critical_events == 1U, "critical event count changed");
  auto batch = manager.DrainForwardingBatch();
  Require(batch.size() == 1U, "forwarding batch size changed");
  Require(manager.DrainForwardingBatch().empty(), "forwarding batch did not drain");

  auto second = manager.Record(FirewallEvent(20U));
  Require(second.HasValue(), "second IDSM event was rejected");
  Require(second.Value().action == idsm::IdsmAction::kAggregated, "aggregation changed");
  Require(second.Value().count == 2U, "aggregated event count changed");
  Require(second.Value().forwarded, "aggregated warning event was not forwarded");

  auto third = manager.Record(FirewallEvent(30U));
  Require(third.HasValue(), "third IDSM event was rejected");
  Require(third.Value().action == idsm::IdsmAction::kThrottled, "throttle action changed");
  Require(!third.Value().forwarded, "throttled event was forwarded");
  Require(manager.Snapshot().throttled_events == 1U, "throttle count changed");

  auto invalid = manager.Record({
    .source = idsm::EventSource::kApplication,
    .category = "app",
    .severity = iam::SecuritySeverity::kWarning,
    .principal_id = "app",
    .resource_id = "resource",
    .operation = iam::Operation::kFindService,
    .detail = "",
    .count = 1U,
    .timestamp_ms = 40U,
  });
  Require(!invalid.HasValue(), "incomplete IDSM event was accepted");

  Require(manager.Record({
            .source = idsm::EventSource::kDiagnostics,
            .category = "uds",
            .severity = iam::SecuritySeverity::kWarning,
            .principal_id = "tester",
            .resource_id = "uds/F189",
            .operation = iam::Operation::kDiagnosticRead,
            .detail = "diagnostic access denied",
            .count = 1U,
            .timestamp_ms = 200U,
          }).HasValue(),
          "diagnostic IDSM event was rejected");
  auto dropped = manager.Record({
    .source = idsm::EventSource::kUpdate,
    .category = "ucm",
    .severity = iam::SecuritySeverity::kCritical,
    .principal_id = "updater",
    .resource_id = "cluster",
    .operation = iam::Operation::kUpdateVerify,
    .detail = "signature rejected",
    .count = 1U,
    .timestamp_ms = 300U,
  });
  Require(dropped.HasValue(), "capacity IDSM event was rejected");
  Require(
    dropped.Value().action == idsm::IdsmAction::kDroppedOldest,
    "oldest event was not dropped at capacity");
  snapshot = manager.Snapshot();
  Require(snapshot.records.size() == 2U, "bounded IDSM record size changed");
  Require(snapshot.dropped_events == 1U, "dropped event count changed");

  idsm::IdsmManager plain_manager{{
    .max_records = 4U,
    .aggregation_window_ms = 100U,
    .rate_limit_count = 4U,
    .rate_limit_window_ms = 100U,
    .forwarding_threshold = iam::SecuritySeverity::kCritical,
    .backend_forwarding_enabled = false,
    .privacy_mode = idsm::PrivacyMode::kPlain,
  }};
  iam::SecurityEvent security_event;
  security_event.source = "ara-com";
  security_event.category = "authorization";
  security_event.severity = iam::SecuritySeverity::kCritical;
  security_event.principal_id = "unknown";
  security_event.resource_id = "0x0A500001/1";
  security_event.operation = iam::Operation::kSubscribeEvent;
  security_event.detail = "subscription denied";
  security_event.timestamp_ms = 500U;
  auto converted = plain_manager.RecordSecurityEvent(security_event);
  Require(converted.HasValue(), "security event conversion failed");
  auto converted_snapshot = plain_manager.Snapshot();
  Require(
    converted_snapshot.records[0U].source == idsm::EventSource::kCommunication,
    "security event source mapping changed");
  Require(
    converted_snapshot.records[0U].principal_id == "unknown",
    "plain privacy mode redacted identifiers");

  Require(idsm::ToString(idsm::EventSource::kFirewall) == std::string_view("firewall"),
          "IDSM source text changed");
  Require(idsm::ToString(idsm::IdsmAction::kThrottled) == std::string_view("throttled"),
          "IDSM action text changed");

  return 0;
}
