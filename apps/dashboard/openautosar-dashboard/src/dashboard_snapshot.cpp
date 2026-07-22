// SPDX-License-Identifier: MIT

#include "openautosar/dashboard/dashboard_snapshot.h"

#include <iomanip>
#include <sstream>

namespace openautosar::dashboard {
namespace {

void WriteQuoted(std::ostream& output, std::string_view value) {
  output << '"';
  for (const char character : value) {
    switch (character) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        output << character;
        break;
    }
  }
  output << '"';
}

void WriteKey(std::ostream& output, std::string_view key, unsigned indent) {
  output << std::string(indent, ' ');
  WriteQuoted(output, key);
  output << ": ";
}

}  // namespace

DashboardSnapshot BuildSnapshot(
  const virtual_vehicle::UltrasonicDiagnosticSnapshot& ultrasonic,
  std::uint64_t service_events,
  const std::vector<FaultHistoryItem>& fault_history,
  const runtime::phm::SupervisionReport& health,
  const runtime::diagnostics::DiagnosticManager& diagnostics,
  runtime::ucm::TransactionState update_state,
  const runtime::ucm::SlotStatus& slots) {
  return {
    .schema = "openautosar.dashboard.v1",
    .machine = "qemux86-64",
    .service = "ultrasonic/front-center",
    .service_state = std::string(virtual_vehicle::ToString(ultrasonic.service_state)),
    .last_valid_distance_mm = ultrasonic.last_valid_distance_mm,
    .service_events = service_events,
    .observed_faults = ultrasonic.observed_faults,
    .phm_health = std::string(runtime::phm::ToString(health.health)),
    .phm_status = std::string(runtime::phm::ToString(health.status)),
    .phm_action = std::string(runtime::phm::ToString(health.action)),
    .diagnostic_session = std::string(runtime::diagnostics::ToString(diagnostics.Session())),
    .diagnostic_security_unlocked = diagnostics.SecurityUnlocked(),
    .dtcs = diagnostics.EventMemory(),
    .fault_history = fault_history,
    .update_state = std::string(runtime::ucm::ToString(update_state)),
    .active_slot = slots.active_slot,
    .inactive_slot = slots.inactive_slot,
    .pending_slot = slots.pending_slot,
  };
}

std::string ToJson(const DashboardSnapshot& snapshot) {
  std::ostringstream output;
  output << "{\n";

  WriteKey(output, "schema", 2U);
  WriteQuoted(output, snapshot.schema);
  output << ",\n";

  WriteKey(output, "machine", 2U);
  WriteQuoted(output, snapshot.machine);
  output << ",\n";

  output << "  \"ultrasonic\": {\n";
  WriteKey(output, "service", 4U);
  WriteQuoted(output, snapshot.service);
  output << ",\n";
  WriteKey(output, "state", 4U);
  WriteQuoted(output, snapshot.service_state);
  output << ",\n";
  WriteKey(output, "last_valid_distance_mm", 4U);
  if (snapshot.last_valid_distance_mm.has_value()) {
    output << snapshot.last_valid_distance_mm.value();
  } else {
    output << "null";
  }
  output << ",\n";
  WriteKey(output, "service_events", 4U);
  output << snapshot.service_events << ",\n";
  WriteKey(output, "observed_faults", 4U);
  output << snapshot.observed_faults << "\n";
  output << "  },\n";

  output << "  \"health\": {\n";
  WriteKey(output, "state", 4U);
  WriteQuoted(output, snapshot.phm_health);
  output << ",\n";
  WriteKey(output, "status", 4U);
  WriteQuoted(output, snapshot.phm_status);
  output << ",\n";
  WriteKey(output, "action", 4U);
  WriteQuoted(output, snapshot.phm_action);
  output << "\n";
  output << "  },\n";

  output << "  \"diagnostics\": {\n";
  WriteKey(output, "session", 4U);
  WriteQuoted(output, snapshot.diagnostic_session);
  output << ",\n";
  WriteKey(output, "security_unlocked", 4U);
  output << BoolText(snapshot.diagnostic_security_unlocked) << ",\n";
  WriteKey(output, "dtc_count", 4U);
  output << snapshot.dtcs.size() << ",\n";
  output << "    \"dtcs\": [\n";
  for (std::size_t index = 0U; index < snapshot.dtcs.size(); ++index) {
    const auto& dtc = snapshot.dtcs[index];
    output << "      {\n";
    WriteKey(output, "code", 8U);
    WriteQuoted(output, Hex24(dtc.code));
    output << ",\n";
    WriteKey(output, "status", 8U);
    output << static_cast<unsigned>(dtc.status) << ",\n";
    WriteKey(output, "origin", 8U);
    WriteQuoted(output, dtc.origin);
    output << ",\n";
    WriteKey(output, "description", 8U);
    WriteQuoted(output, dtc.description);
    output << "\n";
    output << "      }";
    if (index + 1U < snapshot.dtcs.size()) {
      output << ',';
    }
    output << '\n';
  }
  output << "    ]\n";
  output << "  },\n";

  output << "  \"fault_history\": [\n";
  for (std::size_t index = 0U; index < snapshot.fault_history.size(); ++index) {
    const auto& item = snapshot.fault_history[index];
    output << "    {\n";
    WriteKey(output, "sample_index", 6U);
    output << item.sample_index << ",\n";
    WriteKey(output, "fault", 6U);
    WriteQuoted(output, item.fault);
    output << ",\n";
    WriteKey(output, "dtc", 6U);
    WriteQuoted(output, Hex24(item.dtc));
    output << ",\n";
    WriteKey(output, "service_state", 6U);
    WriteQuoted(output, item.service_state);
    output << ",\n";
    WriteKey(output, "degraded_requested", 6U);
    output << BoolText(item.degraded_requested) << "\n";
    output << "    }";
    if (index + 1U < snapshot.fault_history.size()) {
      output << ',';
    }
    output << '\n';
  }
  output << "  ],\n";

  output << "  \"update\": {\n";
  WriteKey(output, "state", 4U);
  WriteQuoted(output, snapshot.update_state);
  output << ",\n";
  WriteKey(output, "active_slot", 4U);
  WriteQuoted(output, snapshot.active_slot);
  output << ",\n";
  WriteKey(output, "inactive_slot", 4U);
  WriteQuoted(output, snapshot.inactive_slot);
  output << ",\n";
  WriteKey(output, "pending_slot", 4U);
  if (snapshot.pending_slot.has_value()) {
    WriteQuoted(output, snapshot.pending_slot.value());
  } else {
    output << "null";
  }
  output << "\n";
  output << "  }\n";
  output << "}\n";

  return output.str();
}

std::string DemoSnapshotJson() {
  runtime::diagnostics::DiagnosticManager diagnostics;
  virtual_vehicle::UltrasonicDiagnosticBridge bridge{diagnostics};
  static_cast<void>(bridge.RegisterContribution());

  const virtual_vehicle::UltrasonicValidationReport report{
    .state = virtual_vehicle::UltrasonicServiceState::kDegraded,
    .faults = {virtual_vehicle::ValidationFault::kCrcMismatch},
    .publish_distance_mm = std::nullopt,
    .last_valid_distance_mm = 1'425U,
    .consecutive_faults = 2U,
    .request_degraded_state = true,
  };
  static_cast<void>(bridge.ObserveValidationReport(report));

  runtime::phm::SupervisionReport health;
  health.entity = "oa-ultrasonic-gateway-smoke";
  health.health = runtime::phm::HealthState::kDegraded;
  health.status = runtime::phm::SupervisionStatus::kServiceUnavailable;
  health.action = runtime::phm::RecoveryAction::kEnterDegradedMode;
  health.timestamp_ms = 30U;
  health.detail = "ultrasonic service unavailable";

  runtime::ucm::SlotStatus slots;
  slots.active_slot = "A";
  slots.inactive_slot = "B";
  slots.inactive_slot_free_bytes = 256U * 1024U * 1024U;

  const std::vector<FaultHistoryItem> history{{
    .sample_index = 3U,
    .fault = std::string(virtual_vehicle::ToString(virtual_vehicle::ValidationFault::kCrcMismatch)),
    .dtc = virtual_vehicle::kUltrasonicDtcCrcMismatch,
    .service_state = std::string(virtual_vehicle::ToString(report.state)),
    .degraded_requested = report.request_degraded_state,
  }};

  return ToJson(BuildSnapshot(
    bridge.Snapshot(),
    2U,
    history,
    health,
    diagnostics,
    runtime::ucm::TransactionState::kIdle,
    slots));
}

std::string Hex24(std::uint32_t value) {
  std::ostringstream output;
  output << "0x" << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
         << (value & 0x00FFFFFFU);
  return output.str();
}

std::string_view BoolText(bool value) noexcept {
  return value ? "true" : "false";
}

}  // namespace openautosar::dashboard
