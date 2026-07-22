// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openautosar::someip {

inline constexpr std::uint8_t kProtocolVersion{1U};
inline constexpr std::size_t kHeaderSize{16U};
inline constexpr std::size_t kSomeIpLengthBase{8U};
inline constexpr std::size_t kMaxUdpPayloadSize{1400U};

enum class MessageType : std::uint8_t {
  kRequest = 0x00U,
  kRequestNoReturn = 0x01U,
  kNotification = 0x02U,
  kResponse = 0x80U,
  kError = 0x81U,
};

enum class ReturnCode : std::uint8_t {
  kOk = 0x00U,
  kNotOk = 0x01U,
  kUnknownService = 0x02U,
  kUnknownMethod = 0x03U,
  kMalformedMessage = 0x09U,
};

struct MessageId final {
  std::uint16_t service_id{0U};
  std::uint16_t method_id{0U};

  friend bool operator==(const MessageId&, const MessageId&) = default;
};

struct RequestId final {
  std::uint16_t client_id{0U};
  std::uint16_t session_id{0U};

  friend bool operator==(const RequestId&, const RequestId&) = default;
};

struct Header final {
  MessageId message_id{};
  RequestId request_id{};
  std::uint8_t protocol_version{kProtocolVersion};
  std::uint8_t interface_version{1U};
  MessageType message_type{MessageType::kRequest};
  ReturnCode return_code{ReturnCode::kOk};

  friend bool operator==(const Header&, const Header&) = default;
};

struct Message final {
  Header header{};
  std::vector<std::uint8_t> payload;

  friend bool operator==(const Message&, const Message&) = default;
};

[[nodiscard]] core::Result<std::vector<std::uint8_t>> SerializeMessage(const Message& message);
[[nodiscard]] core::Result<Message> DeserializeMessage(std::span<const std::uint8_t> bytes);

[[nodiscard]] bool FitsUdpPayload(const Message& message) noexcept;
[[nodiscard]] const char* ToString(MessageType type) noexcept;
[[nodiscard]] const char* ToString(ReturnCode code) noexcept;

}  // namespace openautosar::someip
