// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/socketcan_ultrasonic_frame.h"

#include <algorithm>
#include <span>

namespace openautosar::virtual_vehicle {

canfd_frame EncodeSocketCanUltrasonicFrame(
  const UltrasonicSample& sample,
  UltrasonicFault injected_fault) {
  const auto pdu = EncodeClassicUltrasonicPdu(sample, injected_fault);

  canfd_frame frame{};
  frame.can_id = kClassicUltrasonicCanId;
  frame.len = static_cast<__u8>(pdu.size());
  std::copy(pdu.begin(), pdu.end(), frame.data);
  return frame;
}

core::Result<UltrasonicSample> DecodeSocketCanUltrasonicFrame(const canfd_frame& frame) {
  if ((frame.can_id & CAN_EFF_FLAG) != 0U || (frame.can_id & CAN_RTR_FLAG) != 0U ||
      (frame.can_id & CAN_ERR_FLAG) != 0U) {
    return core::Result<UltrasonicSample>::FromError(
      {"socketcan-ultrasonic-frame", "unsupported CAN frame flags"});
  }

  if (frame.can_id != kClassicUltrasonicCanId) {
    return core::Result<UltrasonicSample>::FromError(
      {"socketcan-ultrasonic-frame", "unexpected CAN identifier"});
  }

  if (frame.len != kClassicUltrasonicPduSize) {
    return core::Result<UltrasonicSample>::FromError(
      {"socketcan-ultrasonic-frame", "unexpected CAN payload length"});
  }

  return DecodeClassicUltrasonicPdu(std::span<const std::uint8_t>(frame.data, frame.len));
}

}  // namespace openautosar::virtual_vehicle
