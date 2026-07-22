// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/virtual_vehicle/ultrasonic_types.h"

#include <cstdint>
#include <optional>

namespace openautosar::virtual_vehicle {

enum class UltrasonicScenario {
  kNormalApproach,
  kNormalRecede,
  kStationaryObstacle,
};

struct UltrasonicScenarioConfig final {
  std::uint16_t sensor_id{1U};
  UltrasonicScenario scenario{UltrasonicScenario::kNormalApproach};
  std::uint32_t start_distance_mm{1'500U};
  std::uint32_t step_mm{50U};
  std::uint64_t period_ns{10'000'000ULL};
};

class DeterministicUltrasonicModel final {
public:
  explicit DeterministicUltrasonicModel(UltrasonicScenarioConfig config);

  [[nodiscard]] std::optional<UltrasonicSample> Next(
    UltrasonicFault injected_fault = UltrasonicFault::kNone);

private:
  [[nodiscard]] UltrasonicSample BuildNominalSample() const;
  [[nodiscard]] std::uint32_t DistanceForTick(std::uint64_t tick) const noexcept;

  UltrasonicScenarioConfig config_;
  std::uint64_t tick_{0U};
  std::uint8_t alive_counter_{0U};
  std::optional<UltrasonicSample> previous_sample_;
};

}  // namespace openautosar::virtual_vehicle
