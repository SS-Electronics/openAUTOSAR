// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/virtual_vehicle/ultrasonic_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openautosar::virtual_vehicle {

inline constexpr std::uint8_t kClassicUltrasonicPduVersion{1U};
inline constexpr std::size_t kClassicUltrasonicPduSize{23U};

using ClassicUltrasonicPdu = std::array<std::uint8_t, kClassicUltrasonicPduSize>;

[[nodiscard]] ClassicUltrasonicPdu EncodeClassicUltrasonicPdu(
  const UltrasonicSample& sample,
  UltrasonicFault injected_fault = UltrasonicFault::kNone);

[[nodiscard]] core::Result<UltrasonicSample> DecodeClassicUltrasonicPdu(
  std::span<const std::uint8_t> pdu);

[[nodiscard]] std::uint16_t ComputeClassicUltrasonicCrc(std::span<const std::uint8_t> bytes);

}  // namespace openautosar::virtual_vehicle
