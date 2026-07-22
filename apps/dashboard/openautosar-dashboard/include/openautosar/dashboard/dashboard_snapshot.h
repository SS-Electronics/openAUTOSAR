// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/runtime/diagnostic_manager.h"
#include "openautosar/runtime/platform_health_manager.h"
#include "openautosar/runtime/update_manager.h"
#include "openautosar/virtual_vehicle/ultrasonic_diagnostic_bridge.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::dashboard {

struct FaultHistoryItem final {
  std::uint32_t sample_index{0U};
  std::string fault;
  std::uint32_t dtc{0U};
  std::string service_state;
  bool degraded_requested{false};
};

struct DashboardSnapshot final {
  std::string schema{"openautosar.dashboard.v1"};
  std::string machine{"qemux86-64"};
  std::string service{"ultrasonic/front-center"};
  std::string service_state;
  std::optional<std::uint32_t> last_valid_distance_mm;
  std::uint64_t service_events{0U};
  std::uint32_t observed_faults{0U};

  std::string phm_health;
  std::string phm_status;
  std::string phm_action;

  std::string diagnostic_session;
  bool diagnostic_security_unlocked{false};
  std::vector<runtime::diagnostics::DtcRecord> dtcs;
  std::vector<FaultHistoryItem> fault_history;

  std::string update_state;
  std::string active_slot;
  std::string inactive_slot;
  std::optional<std::string> pending_slot;
};

[[nodiscard]] DashboardSnapshot BuildSnapshot(
  const virtual_vehicle::UltrasonicDiagnosticSnapshot& ultrasonic,
  std::uint64_t service_events,
  const std::vector<FaultHistoryItem>& fault_history,
  const runtime::phm::SupervisionReport& health,
  const runtime::diagnostics::DiagnosticManager& diagnostics,
  runtime::ucm::TransactionState update_state,
  const runtime::ucm::SlotStatus& slots);

[[nodiscard]] std::string ToJson(const DashboardSnapshot& snapshot);
[[nodiscard]] std::string DemoSnapshotJson();
[[nodiscard]] std::string Hex24(std::uint32_t value);
[[nodiscard]] std::string_view BoolText(bool value) noexcept;

}  // namespace openautosar::dashboard
