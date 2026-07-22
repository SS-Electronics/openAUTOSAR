// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/runtime/diagnostic_manager.h"
#include "openautosar/virtual_vehicle/ultrasonic_signal_validator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace openautosar::virtual_vehicle {

inline constexpr std::uint16_t kUltrasonicDidLastValidDistance{0xA501U};
inline constexpr std::uint16_t kUltrasonicDidServiceState{0xA502U};
inline constexpr std::uint16_t kUltrasonicDidFaultCounter{0xA503U};
inline constexpr std::uint16_t kUltrasonicSelfTestRoutine{0x0201U};

inline constexpr std::uint32_t kUltrasonicDtcCrcMismatch{0x0A5001U};
inline constexpr std::uint32_t kUltrasonicDtcStaleSample{0x0A5002U};
inline constexpr std::uint32_t kUltrasonicDtcAliveCounterJump{0x0A5003U};
inline constexpr std::uint32_t kUltrasonicDtcOutOfRange{0x0A5004U};
inline constexpr std::uint32_t kUltrasonicDtcInvalidQuality{0x0A5005U};
inline constexpr std::uint32_t kUltrasonicDtcDroppedFrame{0x0A5006U};
inline constexpr std::uint32_t kUltrasonicDtcMalformedPdu{0x0A5007U};
inline constexpr std::uint32_t kUltrasonicDtcDegradedQuality{0x0A5008U};

struct UltrasonicDiagnosticSnapshot final {
  UltrasonicServiceState service_state{UltrasonicServiceState::kUnavailable};
  std::optional<std::uint32_t> last_valid_distance_mm;
  std::uint8_t consecutive_faults{0U};
  std::uint32_t observed_faults{0U};
  bool degraded_requested{false};
};

class UltrasonicDiagnosticBridge final {
public:
  explicit UltrasonicDiagnosticBridge(
    runtime::diagnostics::DiagnosticManager& diagnostics) noexcept;

  [[nodiscard]] core::Result<bool> RegisterContribution();
  [[nodiscard]] core::Result<std::size_t> ObserveValidationReport(
    const UltrasonicValidationReport& report);

  [[nodiscard]] const UltrasonicDiagnosticSnapshot& Snapshot() const noexcept {
    return snapshot_;
  }

private:
  [[nodiscard]] std::vector<std::uint8_t> ReadLastValidDistance() const;
  [[nodiscard]] std::vector<std::uint8_t> ReadServiceState() const;
  [[nodiscard]] std::vector<std::uint8_t> ReadFaultCounter() const;
  [[nodiscard]] std::vector<std::uint8_t> RunSelfTest(
    runtime::diagnostics::RoutineControlType control) const;

  runtime::diagnostics::DiagnosticManager& diagnostics_;
  UltrasonicDiagnosticSnapshot snapshot_{};
};

[[nodiscard]] std::uint32_t DtcForValidationFault(ValidationFault fault) noexcept;
[[nodiscard]] std::string_view DiagnosticDescriptionForFault(ValidationFault fault) noexcept;

}  // namespace openautosar::virtual_vehicle
