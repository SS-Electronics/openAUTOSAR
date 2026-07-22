// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace openautosar::virtual_vehicle {

enum class UltrasonicQuality : std::uint8_t {
  kValid = 0U,
  kDegraded = 1U,
  kInvalid = 2U,
};

enum class UltrasonicFault {
  kNone,
  kNoEcho,
  kMultipathNoise,
  kFrozenSample,
  kCounterJump,
  kCrcError,
  kDelayedFrame,
  kDroppedFrame,
  kOutOfRange,
  kSensorReset,
};

enum DiagnosticStatus : std::uint32_t {
  kDiagnosticOk = 0U,
  kDiagnosticNoEcho = 1U << 0U,
  kDiagnosticMultipathNoise = 1U << 1U,
  kDiagnosticFrozenSample = 1U << 2U,
  kDiagnosticCounterJump = 1U << 3U,
  kDiagnosticCrcFaultInjected = 1U << 4U,
  kDiagnosticDelayedFrame = 1U << 5U,
  kDiagnosticOutOfRange = 1U << 6U,
  kDiagnosticSensorReset = 1U << 7U,
};

struct UltrasonicSample final {
  std::uint16_t sensor_id{0U};
  std::uint32_t distance_mm{0U};
  UltrasonicQuality quality{UltrasonicQuality::kInvalid};
  std::uint64_t timestamp_ns{0U};
  std::uint8_t alive_counter{0U};
  std::uint32_t diagnostic_status{kDiagnosticOk};

  friend bool operator==(const UltrasonicSample&, const UltrasonicSample&) = default;
};

[[nodiscard]] const char* ToString(UltrasonicQuality quality) noexcept;
[[nodiscard]] const char* ToString(UltrasonicFault fault) noexcept;

}  // namespace openautosar::virtual_vehicle
