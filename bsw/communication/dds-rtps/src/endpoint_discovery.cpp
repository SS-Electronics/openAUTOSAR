// SPDX-License-Identifier: MIT

#include "openautosar/dds/rtps/endpoint_discovery.h"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openautosar::dds::rtps {
namespace {

inline constexpr std::array<std::uint8_t, 4U> kParameterListCdrLe{0x00U, 0x03U, 0x00U, 0x00U};
inline constexpr std::uint16_t kPidSentinel{0x0001U};
inline constexpr std::uint16_t kPidTopicName{0x0005U};
inline constexpr std::uint16_t kPidTypeName{0x0007U};
inline constexpr std::uint16_t kPidReliability{0x001AU};
inline constexpr std::uint16_t kPidDurability{0x001DU};
inline constexpr std::uint16_t kPidUnicastLocator{0x002FU};
inline constexpr std::uint16_t kPidEndpointGuid{0x005AU};
inline constexpr std::int32_t kLocatorKindUdpV4{1};
inline constexpr std::size_t kLocatorSize{24U};
inline constexpr std::size_t kGuidSize{16U};

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"dds-sedp", message};
}

void WriteU16Le(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void WriteU32Le(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

[[nodiscard]] std::uint16_t ReadU16Le(
  std::span<const std::uint8_t> bytes,
  std::size_t offset) {
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(bytes[offset]) |
    static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32Le(
  std::span<const std::uint8_t> bytes,
  std::size_t offset) {
  return static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(bytes[offset]) |
    static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U |
    static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U |
    static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::size_t Align4(std::size_t value) noexcept {
  return (value + 3U) & ~std::size_t{3U};
}

[[nodiscard]] bool IsAllZero(const EntityId& entity_id) noexcept {
  return std::all_of(entity_id.value.begin(), entity_id.value.end(), [](std::uint8_t value) {
    return value == 0U;
  });
}

[[nodiscard]] bool IsSupportedReliability(std::uint32_t value) noexcept {
  return value == kSedpReliabilityBestEffort || value == kSedpReliabilityReliable;
}

[[nodiscard]] bool IsSupportedDurability(std::uint32_t value) noexcept {
  return value == kSedpDurabilityVolatile || value == kSedpDurabilityTransientLocal;
}

[[nodiscard]] core::Result<std::array<std::uint8_t, 4U>> ParseIpv4Address(
  const std::string& host) {
  in_addr address{};
  if (::inet_pton(AF_INET, host.c_str(), &address) != 1) {
    return core::Result<std::array<std::uint8_t, 4U>>::FromError(
      MakeError("SEDP locator host is not an IPv4 address"));
  }

  const auto* octets = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
  return core::Result<std::array<std::uint8_t, 4U>>::FromValue({
    octets[0U],
    octets[1U],
    octets[2U],
    octets[3U],
  });
}

[[nodiscard]] std::string FormatIpv4Address(std::span<const std::uint8_t> address) {
  return std::to_string(address[0U]) + "." + std::to_string(address[1U]) + "." +
         std::to_string(address[2U]) + "." + std::to_string(address[3U]);
}

void WriteParameter(
  std::vector<std::uint8_t>& bytes,
  std::uint16_t parameter_id,
  std::span<const std::uint8_t> value) {
  WriteU16Le(bytes, parameter_id);
  WriteU16Le(bytes, static_cast<std::uint16_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
  while ((bytes.size() % 4U) != 0U) {
    bytes.push_back(0U);
  }
}

[[nodiscard]] std::vector<std::uint8_t> U32Value(std::uint32_t value) {
  std::vector<std::uint8_t> bytes;
  WriteU32Le(bytes, value);
  return bytes;
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>> StringValue(const std::string& value) {
  if (value.empty()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("SEDP topic or type name is empty"));
  }

  if (value.size() + 5U > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("SEDP topic or type name is too long"));
  }

  std::vector<std::uint8_t> bytes;
  WriteU32Le(bytes, static_cast<std::uint32_t>(value.size() + 1U));
  bytes.insert(bytes.end(), value.begin(), value.end());
  bytes.push_back(0U);
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(bytes));
}

[[nodiscard]] std::vector<std::uint8_t> GuidValue(
  const GuidPrefix& guid_prefix,
  const EntityId& entity_id) {
  std::vector<std::uint8_t> value;
  value.reserve(kGuidSize);
  value.insert(value.end(), guid_prefix.value.begin(), guid_prefix.value.end());
  value.insert(value.end(), entity_id.value.begin(), entity_id.value.end());
  return value;
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>> LocatorValue(
  const UdpEndpointAddress& locator) {
  auto address = ParseIpv4Address(locator.host);
  if (!address) {
    return core::Result<std::vector<std::uint8_t>>::FromError(address.Error());
  }

  std::vector<std::uint8_t> value;
  value.reserve(kLocatorSize);
  WriteU32Le(value, static_cast<std::uint32_t>(kLocatorKindUdpV4));
  WriteU32Le(value, locator.port);
  for (std::size_t index = 0U; index < 12U; ++index) {
    value.push_back(0U);
  }
  value.insert(value.end(), address.Value().begin(), address.Value().end());
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(value));
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>> SerializeSedpPayload(
  const SedpEndpointData& endpoint) {
  if (IsAllZero(endpoint.endpoint_id)) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("SEDP endpoint id is invalid"));
  }

  if (!IsSupportedReliability(endpoint.reliability_kind) ||
      !IsSupportedDurability(endpoint.durability_kind)) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("SEDP QoS policy value is unsupported"));
  }

  auto topic_name = StringValue(endpoint.topic_name);
  if (!topic_name) {
    return core::Result<std::vector<std::uint8_t>>::FromError(topic_name.Error());
  }

  auto type_name = StringValue(endpoint.type_name);
  if (!type_name) {
    return core::Result<std::vector<std::uint8_t>>::FromError(type_name.Error());
  }

  auto locator = LocatorValue(endpoint.unicast_locator);
  if (!locator) {
    return core::Result<std::vector<std::uint8_t>>::FromError(locator.Error());
  }

  std::vector<std::uint8_t> payload;
  payload.insert(payload.end(), kParameterListCdrLe.begin(), kParameterListCdrLe.end());
  WriteParameter(
    payload,
    kPidEndpointGuid,
    GuidValue(endpoint.participant_guid_prefix, endpoint.endpoint_id));
  WriteParameter(payload, kPidTopicName, topic_name.Value());
  WriteParameter(payload, kPidTypeName, type_name.Value());
  WriteParameter(payload, kPidUnicastLocator, locator.Value());
  WriteParameter(payload, kPidReliability, U32Value(endpoint.reliability_kind));
  WriteParameter(payload, kPidDurability, U32Value(endpoint.durability_kind));
  WriteU16Le(payload, kPidSentinel);
  WriteU16Le(payload, 0U);
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(payload));
}

[[nodiscard]] core::Result<std::string> ReadString(std::span<const std::uint8_t> value) {
  if (value.size() < 5U) {
    return core::Result<std::string>::FromError(MakeError("SEDP string parameter is too short"));
  }

  const auto string_size = ReadU32Le(value, 0U);
  if (string_size == 0U || value.size() != 4U + string_size ||
      value[4U + string_size - 1U] != 0U) {
    return core::Result<std::string>::FromError(MakeError("SEDP string parameter is invalid"));
  }

  return core::Result<std::string>::FromValue(std::string{
    value.begin() + static_cast<std::ptrdiff_t>(4U),
    value.begin() + static_cast<std::ptrdiff_t>(4U + string_size - 1U)});
}

[[nodiscard]] core::Result<UdpEndpointAddress> ReadLocator(std::span<const std::uint8_t> value) {
  if (value.size() != kLocatorSize) {
    return core::Result<UdpEndpointAddress>::FromError(MakeError("SEDP locator size is invalid"));
  }

  if (static_cast<std::int32_t>(ReadU32Le(value, 0U)) != kLocatorKindUdpV4) {
    return core::Result<UdpEndpointAddress>::FromError(
      MakeError("SEDP locator kind is unsupported"));
  }

  for (std::size_t index = 8U; index < 20U; ++index) {
    if (value[index] != 0U) {
      return core::Result<UdpEndpointAddress>::FromError(
        MakeError("SEDP locator is not an IPv4 address"));
    }
  }

  const auto port = ReadU32Le(value, 4U);
  if (port > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<UdpEndpointAddress>::FromError(MakeError("SEDP locator port is invalid"));
  }

  return core::Result<UdpEndpointAddress>::FromValue({
    .host = FormatIpv4Address(value.subspan(20U, 4U)),
    .port = static_cast<std::uint16_t>(port),
  });
}

[[nodiscard]] std::pair<EntityId, EntityId> SedpBuiltinPair(SedpEndpointKind kind) noexcept {
  if (kind == SedpEndpointKind::kPublication) {
    return {kSedpBuiltinPublicationsReaderId, kSedpBuiltinPublicationsWriterId};
  }

  return {kSedpBuiltinSubscriptionsReaderId, kSedpBuiltinSubscriptionsWriterId};
}

struct ParseState final {
  SedpEndpointData endpoint{};
  bool endpoint_guid_seen{false};
  bool topic_seen{false};
  bool type_seen{false};
  bool locator_seen{false};
  bool reliability_seen{false};
  bool durability_seen{false};
};

[[nodiscard]] core::Result<bool> ParseParameter(
  ParseState& state,
  std::uint16_t parameter_id,
  std::span<const std::uint8_t> value) {
  switch (parameter_id) {
    case kPidEndpointGuid:
      if (value.size() != kGuidSize) {
        return core::Result<bool>::FromError(MakeError("SEDP endpoint GUID size is invalid"));
      }
      for (std::size_t index = 0U; index < state.endpoint.participant_guid_prefix.value.size();
           ++index) {
        state.endpoint.participant_guid_prefix.value[index] = value[index];
      }
      for (std::size_t index = 0U; index < state.endpoint.endpoint_id.value.size(); ++index) {
        state.endpoint.endpoint_id.value[index] = value[12U + index];
      }
      if (IsAllZero(state.endpoint.endpoint_id)) {
        return core::Result<bool>::FromError(MakeError("SEDP endpoint id is invalid"));
      }
      state.endpoint_guid_seen = true;
      return core::Result<bool>::FromValue(true);
    case kPidTopicName: {
      auto name = ReadString(value);
      if (!name) {
        return core::Result<bool>::FromError(name.Error());
      }
      state.endpoint.topic_name = std::move(name.Value());
      state.topic_seen = true;
      return core::Result<bool>::FromValue(true);
    }
    case kPidTypeName: {
      auto name = ReadString(value);
      if (!name) {
        return core::Result<bool>::FromError(name.Error());
      }
      state.endpoint.type_name = std::move(name.Value());
      state.type_seen = true;
      return core::Result<bool>::FromValue(true);
    }
    case kPidUnicastLocator: {
      auto locator = ReadLocator(value);
      if (!locator) {
        return core::Result<bool>::FromError(locator.Error());
      }
      state.endpoint.unicast_locator = std::move(locator.Value());
      state.locator_seen = true;
      return core::Result<bool>::FromValue(true);
    }
    case kPidReliability:
      if (value.size() != 4U || !IsSupportedReliability(ReadU32Le(value, 0U))) {
        return core::Result<bool>::FromError(MakeError("SEDP reliability policy is unsupported"));
      }
      state.endpoint.reliability_kind = ReadU32Le(value, 0U);
      state.reliability_seen = true;
      return core::Result<bool>::FromValue(true);
    case kPidDurability:
      if (value.size() != 4U || !IsSupportedDurability(ReadU32Le(value, 0U))) {
        return core::Result<bool>::FromError(MakeError("SEDP durability policy is unsupported"));
      }
      state.endpoint.durability_kind = ReadU32Le(value, 0U);
      state.durability_seen = true;
      return core::Result<bool>::FromValue(true);
    default:
      return core::Result<bool>::FromValue(true);
  }
}

[[nodiscard]] core::Result<SedpEndpointData> DeserializeSedpPayload(
  std::span<const std::uint8_t> payload) {
  if (payload.size() < kParameterListCdrLe.size() + 4U) {
    return core::Result<SedpEndpointData>::FromError(
      MakeError("SEDP payload is shorter than parameter list header"));
  }

  if (!std::equal(kParameterListCdrLe.begin(), kParameterListCdrLe.end(), payload.begin())) {
    return core::Result<SedpEndpointData>::FromError(
      MakeError("SEDP parameter list encoding is unsupported"));
  }

  ParseState state;
  std::size_t offset{kParameterListCdrLe.size()};
  bool sentinel_seen{false};
  while (offset + 4U <= payload.size()) {
    const auto parameter_id = ReadU16Le(payload, offset);
    const auto length = ReadU16Le(payload, offset + 2U);
    offset += 4U;

    if (parameter_id == kPidSentinel) {
      if (length != 0U) {
        return core::Result<SedpEndpointData>::FromError(
          MakeError("SEDP sentinel length is invalid"));
      }
      sentinel_seen = true;
      break;
    }

    const auto value_end = offset + length;
    const auto next_offset = Align4(value_end);
    if (value_end > payload.size() || next_offset > payload.size()) {
      return core::Result<SedpEndpointData>::FromError(
        MakeError("SEDP parameter length exceeds payload"));
    }

    auto parsed = ParseParameter(state, parameter_id, payload.subspan(offset, length));
    if (!parsed) {
      return core::Result<SedpEndpointData>::FromError(parsed.Error());
    }
    offset = next_offset;
  }

  if (!sentinel_seen) {
    return core::Result<SedpEndpointData>::FromError(MakeError("SEDP sentinel is missing"));
  }

  if (!state.endpoint_guid_seen || !state.topic_seen || !state.type_seen ||
      !state.locator_seen || !state.reliability_seen || !state.durability_seen) {
    return core::Result<SedpEndpointData>::FromError(
      MakeError("SEDP required endpoint parameter is missing"));
  }

  return core::Result<SedpEndpointData>::FromValue(std::move(state.endpoint));
}

}  // namespace

core::Result<RtpsMessage> BuildSedpEndpointAnnouncement(
  const SedpEndpointData& endpoint,
  SedpEndpointKind kind,
  std::uint64_t sequence_number) {
  if (sequence_number == 0U) {
    return core::Result<RtpsMessage>::FromError(MakeError("SEDP sequence number is zero"));
  }

  auto payload = SerializeSedpPayload(endpoint);
  if (!payload) {
    return core::Result<RtpsMessage>::FromError(payload.Error());
  }

  const auto [reader_id, writer_id] = SedpBuiltinPair(kind);
  RtpsMessage message{
    .vendor_id = {},
    .guid_prefix = endpoint.participant_guid_prefix,
    .data = {{
      .reader_id = reader_id,
      .writer_id = writer_id,
      .writer_sequence_number = sequence_number,
      .serialized_payload = std::move(payload.Value()),
    }},
  };

  if (!FitsUdpPayload(message)) {
    return core::Result<RtpsMessage>::FromError(
      MakeError("SEDP announcement exceeds configured UDP payload limit"));
  }

  return core::Result<RtpsMessage>::FromValue(std::move(message));
}

core::Result<SedpEndpointData> ExtractSedpEndpointAnnouncement(
  const RtpsMessage& message,
  SedpEndpointKind kind) {
  const auto [reader_id, writer_id] = SedpBuiltinPair(kind);
  for (const auto& data : message.data) {
    if (data.writer_id != writer_id || data.reader_id != reader_id) {
      continue;
    }

    auto endpoint = DeserializeSedpPayload(data.serialized_payload);
    if (!endpoint) {
      return core::Result<SedpEndpointData>::FromError(endpoint.Error());
    }

    if (endpoint.Value().participant_guid_prefix != message.guid_prefix) {
      return core::Result<SedpEndpointData>::FromError(
        MakeError("SEDP endpoint does not match RTPS header"));
    }

    return endpoint;
  }

  return core::Result<SedpEndpointData>::FromError(
    MakeError("RTPS message does not contain the requested SEDP endpoint DATA submessage"));
}

}  // namespace openautosar::dds::rtps
