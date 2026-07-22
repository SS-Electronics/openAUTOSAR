// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/virtual_vehicle/classic_ultrasonic_pdu.h"
#include "openautosar/virtual_vehicle/socketcan_ultrasonic_frame.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openautosar::virtual_vehicle {

enum class UltrasonicServiceState {
  kAvailable,
  kUnavailable,
  kDegraded,
};

enum class ValidationFault {
  kMalformedPdu,
  kCrcMismatch,
  kStaleSample,
  kAliveCounterJump,
  kOutOfRange,
  kInvalidQuality,
  kDegradedQuality,
  kDroppedFrame,
};

struct UltrasonicValidationConfig final {
  std::uint32_t min_distance_mm{30U};
  std::uint32_t max_distance_mm{4'500U};
  std::uint64_t max_sample_age_ns{50'000'000ULL};
  std::uint8_t degraded_after_consecutive_faults{2U};
};

struct UltrasonicValidationReport final {
  UltrasonicServiceState state{UltrasonicServiceState::kUnavailable};
  std::vector<ValidationFault> faults;
  std::optional<std::uint32_t> publish_distance_mm;
  std::optional<std::uint32_t> last_valid_distance_mm;
  std::uint8_t consecutive_faults{0U};
  bool request_degraded_state{false};
};

class UltrasonicSignalValidator final {
public:
  explicit UltrasonicSignalValidator(UltrasonicValidationConfig config = {});

  [[nodiscard]] UltrasonicValidationReport ValidateFrame(
    std::span<const std::uint8_t> pdu,
    std::uint64_t now_ns);

  [[nodiscard]] UltrasonicValidationReport ValidateSocketCanFrame(
    const canfd_frame& frame,
    std::uint64_t now_ns);

  [[nodiscard]] UltrasonicValidationReport ObserveDroppedFrame();

private:
  [[nodiscard]] UltrasonicValidationReport BuildFaultReport(ValidationFault fault);
  void RecordFault(UltrasonicValidationReport& report);
  void RecordValid(const UltrasonicSample& sample, UltrasonicValidationReport& report);

  UltrasonicValidationConfig config_;
  std::optional<std::uint8_t> previous_alive_counter_;
  std::optional<std::uint32_t> last_valid_distance_mm_;
  std::uint8_t consecutive_faults_{0U};
};

[[nodiscard]] bool ContainsFault(
  const UltrasonicValidationReport& report,
  ValidationFault fault);

[[nodiscard]] const char* ToString(UltrasonicServiceState state) noexcept;
[[nodiscard]] const char* ToString(ValidationFault fault) noexcept;

}  // namespace openautosar::virtual_vehicle
