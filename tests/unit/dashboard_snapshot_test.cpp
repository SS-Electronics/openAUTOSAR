// SPDX-License-Identifier: MIT

#include "openautosar/dashboard/dashboard_snapshot.h"
#include "openautosar/virtual_vehicle/socketcan_ultrasonic_frame.h"
#include "openautosar/virtual_vehicle/ultrasonic_sensor_model.h"
#include "openautosar/virtual_vehicle/ultrasonic_signal_validator.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

bool Contains(std::string_view value, std::string_view expected) {
  return value.find(expected) != std::string_view::npos;
}

}  // namespace

int main() {
  namespace dash = openautosar::dashboard;
  namespace diag = openautosar::runtime::diagnostics;
  namespace phm = openautosar::runtime::phm;
  namespace ucm = openautosar::runtime::ucm;
  using namespace openautosar::virtual_vehicle;

  diag::DiagnosticManager diagnostics;
  UltrasonicDiagnosticBridge bridge{diagnostics};
  Require(bridge.RegisterContribution().HasValue(), "diagnostic contribution failed");

  DeterministicUltrasonicModel model({
    .sensor_id = 5U,
    .scenario = UltrasonicScenario::kNormalApproach,
    .start_distance_mm = 900U,
    .step_mm = 25U,
    .period_ns = 10'000'000ULL,
  });
  UltrasonicSignalValidator validator({.max_sample_age_ns = 25'000'000ULL});

  const auto valid_sample = model.Next();
  Require(valid_sample.has_value(), "valid ultrasonic sample missing");
  const auto valid_report = validator.ValidateSocketCanFrame(
    EncodeSocketCanUltrasonicFrame(valid_sample.value()),
    valid_sample.value().timestamp_ns + 1'000'000ULL);
  Require(bridge.ObserveValidationReport(valid_report).HasValue(), "valid report not observed");

  const auto crc_sample = model.Next(UltrasonicFault::kCrcError);
  Require(crc_sample.has_value(), "CRC ultrasonic sample missing");
  const auto crc_report = validator.ValidateSocketCanFrame(
    EncodeSocketCanUltrasonicFrame(crc_sample.value(), UltrasonicFault::kCrcError),
    crc_sample.value().timestamp_ns + 1'000'000ULL);
  Require(bridge.ObserveValidationReport(crc_report).HasValue(), "CRC report not observed");

  phm::SupervisionReport health;
  health.entity = "oa-ultrasonic-gateway-smoke";
  health.health = phm::HealthState::kDegraded;
  health.status = phm::SupervisionStatus::kServiceUnavailable;
  health.action = phm::RecoveryAction::kEnterDegradedMode;
  health.timestamp_ms = 20U;
  health.detail = "diagnostic dashboard test";

  ucm::SlotStatus slots;
  slots.active_slot = "A";
  slots.inactive_slot = "B";
  slots.inactive_slot_free_bytes = 256U * 1024U * 1024U;
  const std::vector<dash::FaultHistoryItem> history{{
    .sample_index = 1U,
    .fault = std::string(ToString(ValidationFault::kCrcMismatch)),
    .dtc = kUltrasonicDtcCrcMismatch,
    .service_state = std::string(ToString(crc_report.state)),
    .degraded_requested = crc_report.request_degraded_state,
  }};

  const auto snapshot = dash::BuildSnapshot(
    bridge.Snapshot(),
    1U,
    history,
    health,
    diagnostics,
    ucm::TransactionState::kIdle,
    slots);
  const auto json = dash::ToJson(snapshot);

  Require(Contains(json, "\"schema\": \"openautosar.dashboard.v1\""), "schema missing");
  Require(Contains(json, "\"service_events\": 1"), "service event count missing");
  Require(Contains(json, "\"dtc_count\": 1"), "DTC count missing");
  Require(Contains(json, "\"code\": \"0x0A5001\""), "DTC code missing");
  Require(Contains(json, "\"state\": \"Degraded\""), "PHM degraded state missing");
  Require(Contains(json, "\"last_valid_distance_mm\": 900"), "last valid distance missing");
  Require(!Contains(json, "\"last_valid_distance_mm\": 0"), "invalid zero distance exported");

  const auto demo = dash::DemoSnapshotJson();
  Require(Contains(demo, "\"fault_history\""), "demo fault history missing");
  Require(Contains(demo, "\"update\""), "demo update tile missing");

  return 0;
}
