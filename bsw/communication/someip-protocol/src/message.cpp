// SPDX-License-Identifier: MIT

#include "openautosar/someip/message.h"

#include <limits>

namespace openautosar::someip {
namespace {

void WriteU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
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

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(bytes[offset]) << 24U |
    static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U |
    static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U |
    static_cast<std::uint32_t>(bytes[offset + 3U]));
}

[[nodiscard]] bool IsKnownMessageType(std::uint8_t value) noexcept {
  return value == static_cast<std::uint8_t>(MessageType::kRequest) ||
         value == static_cast<std::uint8_t>(MessageType::kRequestNoReturn) ||
         value == static_cast<std::uint8_t>(MessageType::kNotification) ||
         value == static_cast<std::uint8_t>(MessageType::kResponse) ||
         value == static_cast<std::uint8_t>(MessageType::kError);
}

[[nodiscard]] bool IsKnownReturnCode(std::uint8_t value) noexcept {
  return value == static_cast<std::uint8_t>(ReturnCode::kOk) ||
         value == static_cast<std::uint8_t>(ReturnCode::kNotOk) ||
         value == static_cast<std::uint8_t>(ReturnCode::kUnknownService) ||
         value == static_cast<std::uint8_t>(ReturnCode::kUnknownMethod) ||
         value == static_cast<std::uint8_t>(ReturnCode::kMalformedMessage);
}

}  // namespace

core::Result<std::vector<std::uint8_t>> SerializeMessage(const Message& message) {
  if (!FitsUdpPayload(message)) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      {"someip-protocol", "message exceeds configured UDP payload limit"});
  }

  if (message.header.message_id.service_id == 0U) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      {"someip-protocol", "service id is zero"});
  }

  if (message.header.protocol_version != kProtocolVersion) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      {"someip-protocol", "unsupported protocol version"});
  }

  const auto someip_length = kSomeIpLengthBase + message.payload.size();
  if (someip_length > std::numeric_limits<std::uint32_t>::max()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      {"someip-protocol", "message length overflows SOME/IP header"});
  }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(kHeaderSize + message.payload.size());
  WriteU16(bytes, message.header.message_id.service_id);
  WriteU16(bytes, message.header.message_id.method_id);
  WriteU32(bytes, static_cast<std::uint32_t>(someip_length));
  WriteU16(bytes, message.header.request_id.client_id);
  WriteU16(bytes, message.header.request_id.session_id);
  bytes.push_back(message.header.protocol_version);
  bytes.push_back(message.header.interface_version);
  bytes.push_back(static_cast<std::uint8_t>(message.header.message_type));
  bytes.push_back(static_cast<std::uint8_t>(message.header.return_code));
  bytes.insert(bytes.end(), message.payload.begin(), message.payload.end());
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(bytes));
}

core::Result<Message> DeserializeMessage(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kHeaderSize) {
    return core::Result<Message>::FromError({"someip-protocol", "message shorter than header"});
  }

  const auto length = ReadU32(bytes, 4U);
  if (length < kSomeIpLengthBase) {
    return core::Result<Message>::FromError({"someip-protocol", "invalid SOME/IP length field"});
  }

  const auto payload_size = static_cast<std::size_t>(length) - kSomeIpLengthBase;
  if (bytes.size() != kHeaderSize + payload_size) {
    return core::Result<Message>::FromError({"someip-protocol", "SOME/IP length mismatch"});
  }

  if (bytes[12U] != kProtocolVersion) {
    return core::Result<Message>::FromError({"someip-protocol", "unsupported protocol version"});
  }

  if (!IsKnownMessageType(bytes[14U])) {
    return core::Result<Message>::FromError({"someip-protocol", "unsupported message type"});
  }

  if (!IsKnownReturnCode(bytes[15U])) {
    return core::Result<Message>::FromError({"someip-protocol", "unsupported return code"});
  }

  Message message;
  message.header.message_id.service_id = ReadU16(bytes, 0U);
  message.header.message_id.method_id = ReadU16(bytes, 2U);
  message.header.request_id.client_id = ReadU16(bytes, 8U);
  message.header.request_id.session_id = ReadU16(bytes, 10U);
  message.header.protocol_version = bytes[12U];
  message.header.interface_version = bytes[13U];
  message.header.message_type = static_cast<MessageType>(bytes[14U]);
  message.header.return_code = static_cast<ReturnCode>(bytes[15U]);
  message.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), bytes.end());
  return core::Result<Message>::FromValue(std::move(message));
}

bool FitsUdpPayload(const Message& message) noexcept {
  return kHeaderSize + message.payload.size() <= kMaxUdpPayloadSize;
}

const char* ToString(MessageType type) noexcept {
  switch (type) {
    case MessageType::kRequest:
      return "request";
    case MessageType::kRequestNoReturn:
      return "request-no-return";
    case MessageType::kNotification:
      return "notification";
    case MessageType::kResponse:
      return "response";
    case MessageType::kError:
      return "error";
  }

  return "unknown";
}

const char* ToString(ReturnCode code) noexcept {
  switch (code) {
    case ReturnCode::kOk:
      return "ok";
    case ReturnCode::kNotOk:
      return "not-ok";
    case ReturnCode::kUnknownService:
      return "unknown-service";
    case ReturnCode::kUnknownMethod:
      return "unknown-method";
    case ReturnCode::kMalformedMessage:
      return "malformed-message";
  }

  return "unknown";
}

}  // namespace openautosar::someip
