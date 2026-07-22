// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/dds/rtps/rtps_message.h"
#include "openautosar/dds/rtps/udp_endpoint.h"

#include <cstdint>

namespace openautosar::dds::rtps {

inline constexpr EntityId kParticipantEntityId{.value = {0x00U, 0x00U, 0x01U, 0xC1U}};
inline constexpr EntityId kSpdpBuiltinParticipantWriterId{
  .value = {0x00U, 0x01U, 0x00U, 0xC2U}};
inline constexpr EntityId kSpdpBuiltinParticipantReaderId{
  .value = {0x00U, 0x01U, 0x00U, 0xC7U}};
inline constexpr std::uint32_t kSpdpBuiltinParticipantEndpointSet{0x00000003U};

struct SpdpParticipantData final {
  GuidPrefix guid_prefix{};
  VendorId vendor_id{};
  UdpEndpointAddress metatraffic_unicast_locator{};
  UdpEndpointAddress default_unicast_locator{};
  std::uint32_t builtin_endpoint_set{kSpdpBuiltinParticipantEndpointSet};
  std::uint32_t lease_duration_ms{3'000U};

  friend bool operator==(const SpdpParticipantData&, const SpdpParticipantData&) = default;
};

[[nodiscard]] core::Result<RtpsMessage> BuildSpdpParticipantAnnouncement(
  const SpdpParticipantData& participant,
  std::uint64_t sequence_number);

[[nodiscard]] core::Result<SpdpParticipantData> ExtractSpdpParticipantAnnouncement(
  const RtpsMessage& message);

}  // namespace openautosar::dds::rtps
