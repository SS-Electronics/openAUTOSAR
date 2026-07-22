// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/ultrasonic_sensor_model.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace openautosar::virtual_vehicle {

DeterministicUltrasonicModel::DeterministicUltrasonicModel(UltrasonicScenarioConfig config)
  : config_(std::move(config)) {}

std::optional<UltrasonicSample> DeterministicUltrasonicModel::Next(UltrasonicFault injected_fault) {
  if (injected_fault == UltrasonicFault::kDroppedFrame) {
    ++tick_;
    alive_counter_ = static_cast<std::uint8_t>(alive_counter_ + 1U);
    return std::nullopt;
  }

  if (injected_fault == UltrasonicFault::kFrozenSample && previous_sample_.has_value()) {
    auto frozen = previous_sample_.value();
    frozen.diagnostic_status |= kDiagnosticFrozenSample;
    frozen.quality = UltrasonicQuality::kInvalid;
    return frozen;
  }

  auto sample = BuildNominalSample();

  switch (injected_fault) {
    case UltrasonicFault::kNone:
      break;
    case UltrasonicFault::kNoEcho:
      sample.distance_mm = std::numeric_limits<std::uint32_t>::max();
      sample.quality = UltrasonicQuality::kInvalid;
      sample.diagnostic_status |= kDiagnosticNoEcho;
      break;
    case UltrasonicFault::kMultipathNoise:
      sample.distance_mm = static_cast<std::uint32_t>(sample.distance_mm + ((tick_ % 2U) * 17U));
      sample.quality = UltrasonicQuality::kDegraded;
      sample.diagnostic_status |= kDiagnosticMultipathNoise;
      break;
    case UltrasonicFault::kFrozenSample:
      sample.quality = UltrasonicQuality::kInvalid;
      sample.diagnostic_status |= kDiagnosticFrozenSample;
      break;
    case UltrasonicFault::kCounterJump:
      sample.alive_counter = static_cast<std::uint8_t>(sample.alive_counter + 5U);
      sample.diagnostic_status |= kDiagnosticCounterJump;
      break;
    case UltrasonicFault::kCrcError:
      sample.diagnostic_status |= kDiagnosticCrcFaultInjected;
      break;
    case UltrasonicFault::kDelayedFrame:
      sample.timestamp_ns =
        sample.timestamp_ns > (config_.period_ns * 8U) ? sample.timestamp_ns - (config_.period_ns * 8U) : 0U;
      sample.diagnostic_status |= kDiagnosticDelayedFrame;
      break;
    case UltrasonicFault::kDroppedFrame:
      break;
    case UltrasonicFault::kOutOfRange:
      sample.distance_mm = config_.start_distance_mm + config_.step_mm + 5'000U;
      sample.quality = UltrasonicQuality::kInvalid;
      sample.diagnostic_status |= kDiagnosticOutOfRange;
      break;
    case UltrasonicFault::kSensorReset:
      sample.alive_counter = 0U;
      sample.diagnostic_status |= kDiagnosticSensorReset;
      break;
  }

  previous_sample_ = sample;
  ++tick_;
  alive_counter_ = static_cast<std::uint8_t>(alive_counter_ + 1U);
  return sample;
}

UltrasonicSample DeterministicUltrasonicModel::BuildNominalSample() const {
  UltrasonicSample sample;
  sample.sensor_id = config_.sensor_id;
  sample.distance_mm = DistanceForTick(tick_);
  sample.quality = UltrasonicQuality::kValid;
  sample.timestamp_ns = tick_ * config_.period_ns;
  sample.alive_counter = alive_counter_;
  sample.diagnostic_status = kDiagnosticOk;
  return sample;
}

std::uint32_t DeterministicUltrasonicModel::DistanceForTick(std::uint64_t tick) const noexcept {
  const auto delta = static_cast<std::uint32_t>(
    std::min<std::uint64_t>(tick * config_.step_mm, std::numeric_limits<std::uint32_t>::max()));

  switch (config_.scenario) {
    case UltrasonicScenario::kNormalApproach:
      return delta >= config_.start_distance_mm ? 30U : config_.start_distance_mm - delta;
    case UltrasonicScenario::kNormalRecede:
      return config_.start_distance_mm + delta;
    case UltrasonicScenario::kStationaryObstacle:
      return config_.start_distance_mm;
  }

  return config_.start_distance_mm;
}

}  // namespace openautosar::virtual_vehicle
