// SPDX-License-Identifier: MIT

#include "openautosar/dds/rtps/participant_discovery.h"

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
inline constexpr std::uint16_t kPidParticipantLeaseDuration{0x0002U};
inline constexpr std::uint16_t kPidProtocolVersion{0x0015U};
inline constexpr std::uint16_t kPidVendorId{0x0016U};
inline constexpr std::uint16_t kPidDefaultUnicastLocator{0x0031U};
inline constexpr std::uint16_t kPidMetatrafficUnicastLocator{0x0032U};
inline constexpr std::uint16_t kPidParticipantGuid{0x0050U};
inline constexpr std::uint16_t kPidBuiltinEndpointSet{0x0058U};
inline constexpr std::int32_t kLocatorKindUdpV4{1};
inline constexpr std::size_t kLocatorSize{24U};
inline constexpr std::size_t kGuidSize{16U};

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"dds-spdp", message};
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

[[nodiscard]] bool IsAllZero(const VendorId& vendor_id) noexcept {
  return vendor_id.value[0U] == 0U && vendor_id.value[1U] == 0U;
}

[[nodiscard]] core::Result<std::array<std::uint8_t, 4U>> ParseIpv4Address(
  const std::string& host) {
  in_addr address{};
  if (::inet_pton(AF_INET, host.c_str(), &address) != 1) {
    return core::Result<std::array<std::uint8_t, 4U>>::FromError(
      MakeError("SPDP locator host is not an IPv4 address"));
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

[[nodiscard]] std::vector<std::uint8_t> ProtocolVersionValue() {
  return {kRtpsVersionMajor, kRtpsVersionMinor, 0U, 0U};
}

[[nodiscard]] std::vector<std::uint8_t> VendorIdValue(const VendorId& vendor_id) {
  return {vendor_id.value[0U], vendor_id.value[1U], 0U, 0U};
}

[[nodiscard]] std::vector<std::uint8_t> GuidValue(const GuidPrefix& guid_prefix) {
  std::vector<std::uint8_t> value;
  value.reserve(kGuidSize);
  value.insert(value.end(), guid_prefix.value.begin(), guid_prefix.value.end());
  value.insert(value.end(), kParticipantEntityId.value.begin(), kParticipantEntityId.value.end());
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

[[nodiscard]] std::vector<std::uint8_t> U32Value(std::uint32_t value) {
  std::vector<std::uint8_t> bytes;
  WriteU32Le(bytes, value);
  return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> DurationValue(std::uint32_t duration_ms) {
  std::vector<std::uint8_t> bytes;
  WriteU32Le(bytes, duration_ms / 1'000U);
  WriteU32Le(bytes, (duration_ms % 1'000U) * 1'000'000U);
  return bytes;
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>> SerializeSpdpPayload(
  const SpdpParticipantData& participant) {
  if (IsAllZero(participant.vendor_id)) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("SPDP vendor id is invalid"));
  }

  if (participant.builtin_endpoint_set == 0U || participant.lease_duration_ms == 0U) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("SPDP endpoint set or lease duration is invalid"));
  }

  auto metatraffic_locator = LocatorValue(participant.metatraffic_unicast_locator);
  if (!metatraffic_locator) {
    return core::Result<std::vector<std::uint8_t>>::FromError(metatraffic_locator.Error());
  }

  auto default_locator = LocatorValue(participant.default_unicast_locator);
  if (!default_locator) {
    return core::Result<std::vector<std::uint8_t>>::FromError(default_locator.Error());
  }

  std::vector<std::uint8_t> payload;
  payload.insert(payload.end(), kParameterListCdrLe.begin(), kParameterListCdrLe.end());
  WriteParameter(payload, kPidProtocolVersion, ProtocolVersionValue());
  WriteParameter(payload, kPidVendorId, VendorIdValue(participant.vendor_id));
  WriteParameter(payload, kPidParticipantGuid, GuidValue(participant.guid_prefix));
  WriteParameter(payload, kPidMetatrafficUnicastLocator, metatraffic_locator.Value());
  WriteParameter(payload, kPidDefaultUnicastLocator, default_locator.Value());
  WriteParameter(payload, kPidBuiltinEndpointSet, U32Value(participant.builtin_endpoint_set));
  WriteParameter(
    payload,
    kPidParticipantLeaseDuration,
    DurationValue(participant.lease_duration_ms));
  WriteU16Le(payload, kPidSentinel);
  WriteU16Le(payload, 0U);
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(payload));
}

[[nodiscard]] core::Result<UdpEndpointAddress> ReadLocator(std::span<const std::uint8_t> value) {
  if (value.size() != kLocatorSize) {
    return core::Result<UdpEndpointAddress>::FromError(MakeError("SPDP locator size is invalid"));
  }

  if (static_cast<std::int32_t>(ReadU32Le(value, 0U)) != kLocatorKindUdpV4) {
    return core::Result<UdpEndpointAddress>::FromError(
      MakeError("SPDP locator kind is unsupported"));
  }

  for (std::size_t index = 8U; index < 20U; ++index) {
    if (value[index] != 0U) {
      return core::Result<UdpEndpointAddress>::FromError(
        MakeError("SPDP locator is not an IPv4 address"));
    }
  }

  const auto port = ReadU32Le(value, 4U);
  if (port > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<UdpEndpointAddress>::FromError(MakeError("SPDP locator port is invalid"));
  }

  return core::Result<UdpEndpointAddress>::FromValue({
    .host = FormatIpv4Address(value.subspan(20U, 4U)),
    .port = static_cast<std::uint16_t>(port),
  });
}

struct ParseState final {
  SpdpParticipantData participant{};
  bool protocol_version_seen{false};
  bool vendor_id_seen{false};
  bool guid_seen{false};
  bool metatraffic_locator_seen{false};
  bool default_locator_seen{false};
  bool endpoint_set_seen{false};
  bool lease_seen{false};
};

[[nodiscard]] core::Result<bool> ParseParameter(
  ParseState& state,
  std::uint16_t parameter_id,
  std::span<const std::uint8_t> value) {
  switch (parameter_id) {
    case kPidProtocolVersion:
      if (value.size() != 4U || value[0U] != kRtpsVersionMajor || value[1U] != kRtpsVersionMinor) {
        return core::Result<bool>::FromError(MakeError("SPDP protocol version is unsupported"));
      }
      state.protocol_version_seen = true;
      return core::Result<bool>::FromValue(true);
    case kPidVendorId:
      if (value.size() != 4U) {
        return core::Result<bool>::FromError(MakeError("SPDP vendor id size is invalid"));
      }
      state.participant.vendor_id.value = {value[0U], value[1U]};
      if (IsAllZero(state.participant.vendor_id)) {
        return core::Result<bool>::FromError(MakeError("SPDP vendor id is invalid"));
      }
      state.vendor_id_seen = true;
      return core::Result<bool>::FromValue(true);
    case kPidParticipantGuid:
      if (value.size() != kGuidSize) {
        return core::Result<bool>::FromError(MakeError("SPDP participant GUID size is invalid"));
      }
      for (std::size_t index = 0U; index < state.participant.guid_prefix.value.size(); ++index) {
        state.participant.guid_prefix.value[index] = value[index];
      }
      if (!std::equal(
            kParticipantEntityId.value.begin(),
            kParticipantEntityId.value.end(),
            value.begin() + static_cast<std::ptrdiff_t>(12U))) {
        return core::Result<bool>::FromError(MakeError("SPDP participant entity id is invalid"));
      }
      state.guid_seen = true;
      return core::Result<bool>::FromValue(true);
    case kPidMetatrafficUnicastLocator: {
      auto locator = ReadLocator(value);
      if (!locator) {
        return core::Result<bool>::FromError(locator.Error());
      }
      state.participant.metatraffic_unicast_locator = std::move(locator.Value());
      state.metatraffic_locator_seen = true;
      return core::Result<bool>::FromValue(true);
    }
    case kPidDefaultUnicastLocator: {
      auto locator = ReadLocator(value);
      if (!locator) {
        return core::Result<bool>::FromError(locator.Error());
      }
      state.participant.default_unicast_locator = std::move(locator.Value());
      state.default_locator_seen = true;
      return core::Result<bool>::FromValue(true);
    }
    case kPidBuiltinEndpointSet:
      if (value.size() != 4U) {
        return core::Result<bool>::FromError(
          MakeError("SPDP builtin endpoint set size is invalid"));
      }
      state.participant.builtin_endpoint_set = ReadU32Le(value, 0U);
      state.endpoint_set_seen = true;
      return core::Result<bool>::FromValue(true);
    case kPidParticipantLeaseDuration: {
      if (value.size() != 8U) {
        return core::Result<bool>::FromError(MakeError("SPDP lease duration size is invalid"));
      }
      const auto seconds = ReadU32Le(value, 0U);
      const auto nanoseconds = ReadU32Le(value, 4U);
      if (seconds > std::numeric_limits<std::uint32_t>::max() / 1'000U) {
        return core::Result<bool>::FromError(MakeError("SPDP lease duration overflows"));
      }
      state.participant.lease_duration_ms = (seconds * 1'000U) + (nanoseconds / 1'000'000U);
      state.lease_seen = true;
      return core::Result<bool>::FromValue(true);
    }
    default:
      return core::Result<bool>::FromValue(true);
  }
}

[[nodiscard]] core::Result<SpdpParticipantData> DeserializeSpdpPayload(
  std::span<const std::uint8_t> payload) {
  if (payload.size() < kParameterListCdrLe.size() + 4U) {
    return core::Result<SpdpParticipantData>::FromError(
      MakeError("SPDP payload is shorter than parameter list header"));
  }

  if (!std::equal(kParameterListCdrLe.begin(), kParameterListCdrLe.end(), payload.begin())) {
    return core::Result<SpdpParticipantData>::FromError(
      MakeError("SPDP parameter list encoding is unsupported"));
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
        return core::Result<SpdpParticipantData>::FromError(
          MakeError("SPDP sentinel length is invalid"));
      }
      sentinel_seen = true;
      break;
    }

    const auto value_end = offset + length;
    const auto next_offset = Align4(value_end);
    if (value_end > payload.size() || next_offset > payload.size()) {
      return core::Result<SpdpParticipantData>::FromError(
        MakeError("SPDP parameter length exceeds payload"));
    }

    auto parsed = ParseParameter(state, parameter_id, payload.subspan(offset, length));
    if (!parsed) {
      return core::Result<SpdpParticipantData>::FromError(parsed.Error());
    }
    offset = next_offset;
  }

  if (!sentinel_seen) {
    return core::Result<SpdpParticipantData>::FromError(MakeError("SPDP sentinel is missing"));
  }

  if (!state.protocol_version_seen || !state.vendor_id_seen || !state.guid_seen ||
      !state.metatraffic_locator_seen || !state.default_locator_seen ||
      !state.endpoint_set_seen || !state.lease_seen) {
    return core::Result<SpdpParticipantData>::FromError(
      MakeError("SPDP required participant parameter is missing"));
  }

  return core::Result<SpdpParticipantData>::FromValue(std::move(state.participant));
}

}  // namespace

core::Result<RtpsMessage> BuildSpdpParticipantAnnouncement(
  const SpdpParticipantData& participant,
  std::uint64_t sequence_number) {
  if (sequence_number == 0U) {
    return core::Result<RtpsMessage>::FromError(MakeError("SPDP sequence number is zero"));
  }

  auto payload = SerializeSpdpPayload(participant);
  if (!payload) {
    return core::Result<RtpsMessage>::FromError(payload.Error());
  }

  RtpsMessage message{
    .vendor_id = participant.vendor_id,
    .guid_prefix = participant.guid_prefix,
    .data = {{
      .reader_id = kSpdpBuiltinParticipantReaderId,
      .writer_id = kSpdpBuiltinParticipantWriterId,
      .writer_sequence_number = sequence_number,
      .serialized_payload = std::move(payload.Value()),
    }},
  };

  if (!FitsUdpPayload(message)) {
    return core::Result<RtpsMessage>::FromError(
      MakeError("SPDP announcement exceeds configured UDP payload limit"));
  }

  return core::Result<RtpsMessage>::FromValue(std::move(message));
}

core::Result<SpdpParticipantData> ExtractSpdpParticipantAnnouncement(
  const RtpsMessage& message) {
  for (const auto& data : message.data) {
    if (data.writer_id != kSpdpBuiltinParticipantWriterId ||
        data.reader_id != kSpdpBuiltinParticipantReaderId) {
      continue;
    }

    auto participant = DeserializeSpdpPayload(data.serialized_payload);
    if (!participant) {
      return core::Result<SpdpParticipantData>::FromError(participant.Error());
    }

    if (participant.Value().guid_prefix != message.guid_prefix ||
        participant.Value().vendor_id != message.vendor_id) {
      return core::Result<SpdpParticipantData>::FromError(
        MakeError("SPDP participant does not match RTPS header"));
    }

    return participant;
  }

  return core::Result<SpdpParticipantData>::FromError(
    MakeError("RTPS message does not contain an SPDP participant DATA submessage"));
}

}  // namespace openautosar::dds::rtps
