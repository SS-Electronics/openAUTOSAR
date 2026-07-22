// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/ultrasonic_diagnostic_bridge.h"

#include <algorithm>
#include <utility>

namespace openautosar::virtual_vehicle {
namespace {

using openautosar::runtime::diagnostics::DataIdentifier;
using openautosar::runtime::diagnostics::DiagnosticContribution;
using openautosar::runtime::diagnostics::DtcStatus;
using openautosar::runtime::diagnostics::Routine;

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"gateway-diagnostics", message};
}

[[nodiscard]] std::uint8_t StatusForReport(
  const UltrasonicValidationReport& report) noexcept {
  std::uint8_t status = static_cast<std::uint8_t>(
    static_cast<std::uint8_t>(DtcStatus::kTestFailed) |
    static_cast<std::uint8_t>(DtcStatus::kConfirmed));
  if (report.request_degraded_state) {
    status = static_cast<std::uint8_t>(
      status | static_cast<std::uint8_t>(DtcStatus::kWarningIndicatorRequested));
  }
  return status;
}

[[nodiscard]] std::vector<std::uint8_t> U16(std::uint16_t value) {
  return {
    static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
    static_cast<std::uint8_t>(value & 0xFFU),
  };
}

[[nodiscard]] std::vector<std::uint8_t> U32(std::uint32_t value) {
  return {
    static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
    static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
    static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
    static_cast<std::uint8_t>(value & 0xFFU),
  };
}

[[nodiscard]] std::uint8_t ServiceStateByte(UltrasonicServiceState state) noexcept {
  switch (state) {
    case UltrasonicServiceState::kAvailable:
      return 0x00U;
    case UltrasonicServiceState::kUnavailable:
      return 0x01U;
    case UltrasonicServiceState::kDegraded:
      return 0x02U;
  }

  return 0xFFU;
}

[[nodiscard]] std::uint8_t BoolByte(bool value) noexcept {
  return value ? static_cast<std::uint8_t>(0x01U) : static_cast<std::uint8_t>(0x00U);
}

}  // namespace

UltrasonicDiagnosticBridge::UltrasonicDiagnosticBridge(
  runtime::diagnostics::DiagnosticManager& diagnostics) noexcept
  : diagnostics_(diagnostics) {}

core::Result<bool> UltrasonicDiagnosticBridge::RegisterContribution() {
  DiagnosticContribution contribution{
    .data_identifiers = {
      DataIdentifier{
        .id = kUltrasonicDidLastValidDistance,
        .name = "ultrasonic-last-valid-distance-mm",
        .read = [this] { return ReadLastValidDistance(); },
      },
      DataIdentifier{
        .id = kUltrasonicDidServiceState,
        .name = "ultrasonic-service-state",
        .read = [this] { return ReadServiceState(); },
      },
      DataIdentifier{
        .id = kUltrasonicDidFaultCounter,
        .name = "ultrasonic-fault-counter",
        .read = [this] { return ReadFaultCounter(); },
      },
    },
    .routines = {
      Routine{
        .id = kUltrasonicSelfTestRoutine,
        .name = "ultrasonic-gateway-self-test",
        .control = [this](runtime::diagnostics::RoutineControlType control) {
          return RunSelfTest(control);
        },
      },
    },
  };

  return diagnostics_.RegisterContribution(std::move(contribution));
}

core::Result<std::size_t> UltrasonicDiagnosticBridge::ObserveValidationReport(
  const UltrasonicValidationReport& report) {
  snapshot_.service_state = report.state;
  snapshot_.last_valid_distance_mm = report.last_valid_distance_mm;
  snapshot_.consecutive_faults = report.consecutive_faults;
  snapshot_.degraded_requested = report.request_degraded_state;

  if (report.faults.empty()) {
    return core::Result<std::size_t>::FromValue(0U);
  }

  std::size_t reported{0U};
  for (const auto fault : report.faults) {
    const auto dtc = DtcForValidationFault(fault);
    if (dtc == 0U) {
      return core::Result<std::size_t>::FromError(
        MakeError("validation fault has no diagnostic mapping"));
    }

    auto stored = diagnostics_.ReportDtc({
      .code = dtc,
      .status = StatusForReport(report),
      .origin = "oa-ultrasonic-gateway-smoke",
      .description = std::string(DiagnosticDescriptionForFault(fault)),
    });
    if (!stored) {
      return core::Result<std::size_t>::FromError(stored.Error());
    }

    ++reported;
    if (snapshot_.observed_faults < 0xFFFFFFFFU) {
      ++snapshot_.observed_faults;
    }
  }

  return core::Result<std::size_t>::FromValue(reported);
}

std::vector<std::uint8_t> UltrasonicDiagnosticBridge::ReadLastValidDistance() const {
  if (!snapshot_.last_valid_distance_mm.has_value()) {
    return {0xFFU, 0xFFU};
  }

  const auto bounded = std::min(snapshot_.last_valid_distance_mm.value(), 0xFFFEU);
  return U16(static_cast<std::uint16_t>(bounded));
}

std::vector<std::uint8_t> UltrasonicDiagnosticBridge::ReadServiceState() const {
  return {
    ServiceStateByte(snapshot_.service_state),
    BoolByte(snapshot_.degraded_requested),
  };
}

std::vector<std::uint8_t> UltrasonicDiagnosticBridge::ReadFaultCounter() const {
  return U32(snapshot_.observed_faults);
}

std::vector<std::uint8_t> UltrasonicDiagnosticBridge::RunSelfTest(
  runtime::diagnostics::RoutineControlType control) const {
  return {
    static_cast<std::uint8_t>(control),
    ServiceStateByte(snapshot_.service_state),
    snapshot_.consecutive_faults,
    BoolByte(snapshot_.degraded_requested),
  };
}

std::uint32_t DtcForValidationFault(ValidationFault fault) noexcept {
  switch (fault) {
    case ValidationFault::kMalformedPdu:
      return kUltrasonicDtcMalformedPdu;
    case ValidationFault::kCrcMismatch:
      return kUltrasonicDtcCrcMismatch;
    case ValidationFault::kStaleSample:
      return kUltrasonicDtcStaleSample;
    case ValidationFault::kAliveCounterJump:
      return kUltrasonicDtcAliveCounterJump;
    case ValidationFault::kOutOfRange:
      return kUltrasonicDtcOutOfRange;
    case ValidationFault::kInvalidQuality:
      return kUltrasonicDtcInvalidQuality;
    case ValidationFault::kDegradedQuality:
      return kUltrasonicDtcDegradedQuality;
    case ValidationFault::kDroppedFrame:
      return kUltrasonicDtcDroppedFrame;
  }

  return 0U;
}

std::string_view DiagnosticDescriptionForFault(ValidationFault fault) noexcept {
  switch (fault) {
    case ValidationFault::kMalformedPdu:
      return "ultrasonic malformed PDU";
    case ValidationFault::kCrcMismatch:
      return "ultrasonic CRC mismatch";
    case ValidationFault::kStaleSample:
      return "ultrasonic stale sample";
    case ValidationFault::kAliveCounterJump:
      return "ultrasonic alive counter jump";
    case ValidationFault::kOutOfRange:
      return "ultrasonic distance out of range";
    case ValidationFault::kInvalidQuality:
      return "ultrasonic invalid quality";
    case ValidationFault::kDegradedQuality:
      return "ultrasonic degraded quality";
    case ValidationFault::kDroppedFrame:
      return "ultrasonic dropped frame";
  }

  return "ultrasonic unknown validation fault";
}

}  // namespace openautosar::virtual_vehicle
