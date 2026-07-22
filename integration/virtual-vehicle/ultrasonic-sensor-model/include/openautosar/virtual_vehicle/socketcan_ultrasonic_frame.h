// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/virtual_vehicle/classic_ultrasonic_pdu.h"

#include <linux/can.h>

namespace openautosar::virtual_vehicle {

inline constexpr canid_t kClassicUltrasonicCanId{0x321U};

[[nodiscard]] canfd_frame EncodeSocketCanUltrasonicFrame(
  const UltrasonicSample& sample,
  UltrasonicFault injected_fault = UltrasonicFault::kNone);

[[nodiscard]] core::Result<UltrasonicSample> DecodeSocketCanUltrasonicFrame(
  const canfd_frame& frame);

}  // namespace openautosar::virtual_vehicle
