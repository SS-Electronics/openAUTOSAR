// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/ultrasonic_types.h"

namespace openautosar::virtual_vehicle {

const char* ToString(UltrasonicQuality quality) noexcept {
  switch (quality) {
    case UltrasonicQuality::kValid:
      return "VALID";
    case UltrasonicQuality::kDegraded:
      return "DEGRADED";
    case UltrasonicQuality::kInvalid:
      return "INVALID";
  }

  return "UNKNOWN";
}

const char* ToString(UltrasonicFault fault) noexcept {
  switch (fault) {
    case UltrasonicFault::kNone:
      return "none";
    case UltrasonicFault::kNoEcho:
      return "no_echo";
    case UltrasonicFault::kMultipathNoise:
      return "multipath_noise";
    case UltrasonicFault::kFrozenSample:
      return "frozen_sample";
    case UltrasonicFault::kCounterJump:
      return "counter_jump";
    case UltrasonicFault::kCrcError:
      return "crc_error";
    case UltrasonicFault::kDelayedFrame:
      return "delayed_frame";
    case UltrasonicFault::kDroppedFrame:
      return "dropped_frame";
    case UltrasonicFault::kOutOfRange:
      return "out_of_range";
    case UltrasonicFault::kSensorReset:
      return "sensor_reset";
  }

  return "unknown";
}

}  // namespace openautosar::virtual_vehicle
