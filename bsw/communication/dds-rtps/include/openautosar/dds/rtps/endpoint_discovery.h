// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/dds/rtps/rtps_message.h"
#include "openautosar/dds/rtps/udp_endpoint.h"

#include <cstdint>
#include <string>

namespace openautosar::dds::rtps {

inline constexpr EntityId kSedpBuiltinPublicationsWriterId{
  .value = {0x00U, 0x00U, 0x03U, 0xC2U}};
inline constexpr EntityId kSedpBuiltinPublicationsReaderId{
  .value = {0x00U, 0x00U, 0x03U, 0xC7U}};
inline constexpr EntityId kSedpBuiltinSubscriptionsWriterId{
  .value = {0x00U, 0x00U, 0x04U, 0xC2U}};
inline constexpr EntityId kSedpBuiltinSubscriptionsReaderId{
  .value = {0x00U, 0x00U, 0x04U, 0xC7U}};
inline constexpr std::uint32_t kSedpReliabilityBestEffort{1U};
inline constexpr std::uint32_t kSedpReliabilityReliable{3U};
inline constexpr std::uint32_t kSedpDurabilityVolatile{0U};
inline constexpr std::uint32_t kSedpDurabilityTransientLocal{1U};

enum class SedpEndpointKind : std::uint8_t {
  kPublication,
  kSubscription,
};

struct SedpEndpointData final {
  GuidPrefix participant_guid_prefix{};
  EntityId endpoint_id{};
  std::string topic_name;
  std::string type_name;
  UdpEndpointAddress unicast_locator{};
  std::uint32_t reliability_kind{kSedpReliabilityBestEffort};
  std::uint32_t durability_kind{kSedpDurabilityVolatile};

  friend bool operator==(const SedpEndpointData&, const SedpEndpointData&) = default;
};

[[nodiscard]] core::Result<RtpsMessage> BuildSedpEndpointAnnouncement(
  const SedpEndpointData& endpoint,
  SedpEndpointKind kind,
  std::uint64_t sequence_number);

[[nodiscard]] core::Result<SedpEndpointData> ExtractSedpEndpointAnnouncement(
  const RtpsMessage& message,
  SedpEndpointKind kind);

}  // namespace openautosar::dds::rtps
