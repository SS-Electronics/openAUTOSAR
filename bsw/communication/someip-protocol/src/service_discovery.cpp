// SPDX-License-Identifier: MIT

#include "openautosar/someip/service_discovery.h"

#include <utility>

namespace openautosar::someip::sd {
namespace {

inline constexpr std::size_t kSdHeaderSize{8U};
inline constexpr std::size_t kSdEntrySize{16U};

void WriteU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void WriteU24(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void WriteU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

[[nodiscard]] std::uint16_t ReadU16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(bytes[offset]) << 8U |
    static_cast<std::uint16_t>(bytes[offset + 1U]));
}

[[nodiscard]] std::uint32_t ReadU24(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(bytes[offset]) << 16U |
    static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U |
    static_cast<std::uint32_t>(bytes[offset + 2U]));
}

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(bytes[offset]) << 24U |
    static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U |
    static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U |
    static_cast<std::uint32_t>(bytes[offset + 3U]));
}

[[nodiscard]] bool IsKnownEntryType(std::uint8_t value) noexcept {
  return value == static_cast<std::uint8_t>(EntryType::kFindService) ||
         value == static_cast<std::uint8_t>(EntryType::kOfferService) ||
         value == static_cast<std::uint8_t>(EntryType::kSubscribeEventgroup) ||
         value == static_cast<std::uint8_t>(EntryType::kSubscribeEventgroupAck);
}

}  // namespace

core::Result<std::vector<std::uint8_t>> SerializeServiceDiscoveryPayload(
  const ServiceDiscoveryMessage& message) {
  if (message.entries.empty()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      {"someip-sd", "service discovery message has no entries"});
  }

  std::vector<std::uint8_t> payload;
  payload.reserve(kSdHeaderSize + (message.entries.size() * kSdEntrySize));
  WriteU32(payload, message.reboot_session);
  WriteU32(payload, static_cast<std::uint32_t>(message.entries.size() * kSdEntrySize));

  for (const auto& entry : message.entries) {
    if (entry.service_id == 0U || entry.instance_id == 0U) {
      return core::Result<std::vector<std::uint8_t>>::FromError(
        {"someip-sd", "service discovery entry has invalid service or instance id"});
    }

    if (entry.major_version == 0U) {
      return core::Result<std::vector<std::uint8_t>>::FromError(
        {"someip-sd", "service discovery entry has invalid major version"});
    }

    if (entry.ttl > 0x00FFFFFFU) {
      return core::Result<std::vector<std::uint8_t>>::FromError(
        {"someip-sd", "service discovery ttl exceeds 24-bit field"});
    }

    payload.push_back(static_cast<std::uint8_t>(entry.type));
    payload.push_back(0U);
    WriteU16(payload, entry.service_id);
    WriteU16(payload, entry.instance_id);
    payload.push_back(entry.major_version);
    WriteU24(payload, entry.ttl);
    WriteU32(payload, entry.minor_version);
    WriteU16(payload, entry.eventgroup_id);
  }

  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(payload));
}

core::Result<ServiceDiscoveryMessage> DeserializeServiceDiscoveryPayload(
  std::span<const std::uint8_t> payload) {
  if (payload.size() < kSdHeaderSize) {
    return core::Result<ServiceDiscoveryMessage>::FromError(
      {"someip-sd", "service discovery payload shorter than header"});
  }

  const auto entries_length = ReadU32(payload, 4U);
  if (entries_length == 0U || entries_length % kSdEntrySize != 0U) {
    return core::Result<ServiceDiscoveryMessage>::FromError(
      {"someip-sd", "invalid service discovery entries length"});
  }

  if (payload.size() != kSdHeaderSize + entries_length) {
    return core::Result<ServiceDiscoveryMessage>::FromError(
      {"someip-sd", "service discovery payload length mismatch"});
  }

  ServiceDiscoveryMessage message;
  message.reboot_session = ReadU32(payload, 0U);
  const auto entry_count = entries_length / kSdEntrySize;
  message.entries.reserve(entry_count);

  for (std::size_t index = 0U; index < entry_count; ++index) {
    const auto offset = kSdHeaderSize + (index * kSdEntrySize);
    if (!IsKnownEntryType(payload[offset])) {
      return core::Result<ServiceDiscoveryMessage>::FromError(
        {"someip-sd", "unsupported service discovery entry type"});
    }

    if (payload[offset + 1U] != 0U) {
      return core::Result<ServiceDiscoveryMessage>::FromError(
        {"someip-sd", "service discovery reserved byte is non-zero"});
    }

    ServiceEntry entry;
    entry.type = static_cast<EntryType>(payload[offset]);
    entry.service_id = ReadU16(payload, offset + 2U);
    entry.instance_id = ReadU16(payload, offset + 4U);
    entry.major_version = payload[offset + 6U];
    entry.ttl = ReadU24(payload, offset + 7U);
    entry.minor_version = ReadU32(payload, offset + 10U);
    entry.eventgroup_id = ReadU16(payload, offset + 14U);
    message.entries.push_back(entry);
  }

  return core::Result<ServiceDiscoveryMessage>::FromValue(std::move(message));
}

core::Result<Message> BuildServiceDiscoveryMessage(
  ServiceDiscoveryMessage discovery,
  RequestId request_id) {
  auto payload = SerializeServiceDiscoveryPayload(discovery);
  if (!payload) {
    return core::Result<Message>::FromError(payload.Error());
  }

  Message message;
  message.header.message_id = kServiceDiscoveryMessageId;
  message.header.request_id = request_id;
  message.header.interface_version = kServiceDiscoveryInterfaceVersion;
  message.header.message_type = MessageType::kNotification;
  message.header.return_code = ReturnCode::kOk;
  message.payload = std::move(payload.Value());
  return core::Result<Message>::FromValue(std::move(message));
}

core::Result<ServiceDiscoveryMessage> ExtractServiceDiscoveryMessage(const Message& message) {
  if (message.header.message_id != kServiceDiscoveryMessageId) {
    return core::Result<ServiceDiscoveryMessage>::FromError(
      {"someip-sd", "message is not a service discovery message"});
  }

  if (message.header.message_type != MessageType::kNotification) {
    return core::Result<ServiceDiscoveryMessage>::FromError(
      {"someip-sd", "service discovery message is not a notification"});
  }

  if (message.header.interface_version != kServiceDiscoveryInterfaceVersion) {
    return core::Result<ServiceDiscoveryMessage>::FromError(
      {"someip-sd", "unsupported service discovery interface version"});
  }

  return DeserializeServiceDiscoveryPayload(message.payload);
}

const char* ToString(EntryType type) noexcept {
  switch (type) {
    case EntryType::kFindService:
      return "find-service";
    case EntryType::kOfferService:
      return "offer-service";
    case EntryType::kSubscribeEventgroup:
      return "subscribe-eventgroup";
    case EntryType::kSubscribeEventgroupAck:
      return "subscribe-eventgroup-ack";
  }

  return "unknown";
}

}  // namespace openautosar::someip::sd
