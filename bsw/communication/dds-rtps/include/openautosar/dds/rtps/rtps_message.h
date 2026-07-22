// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openautosar::dds::rtps {

inline constexpr std::uint8_t kRtpsVersionMajor{2U};
inline constexpr std::uint8_t kRtpsVersionMinor{3U};
inline constexpr std::array<std::uint8_t, 2U> kOpenAutosarVendorId{0x4FU, 0x41U};
inline constexpr std::uint8_t kDataSubmessageKind{0x15U};
inline constexpr std::uint8_t kHeartbeatSubmessageKind{0x07U};
inline constexpr std::uint8_t kAckNackSubmessageKind{0x06U};
inline constexpr std::uint8_t kLittleEndianFlag{0x01U};
inline constexpr std::uint8_t kDataPayloadFlag{0x04U};
inline constexpr std::uint8_t kFinalFlag{0x02U};
inline constexpr std::uint8_t kLivelinessFlag{0x04U};
inline constexpr std::size_t kRtpsHeaderSize{20U};
inline constexpr std::size_t kDataSubmessageHeaderSize{24U};
inline constexpr std::size_t kHeartbeatSubmessageHeaderSize{32U};
inline constexpr std::size_t kAckNackSubmessageHeaderSize{28U};
inline constexpr std::size_t kMaxUdpPayloadSize{1400U};
inline constexpr std::uint32_t kMaxAckNackBitmapBits{256U};

struct VendorId final {
  std::array<std::uint8_t, 2U> value{kOpenAutosarVendorId};

  friend bool operator==(const VendorId&, const VendorId&) = default;
};

struct GuidPrefix final {
  std::array<std::uint8_t, 12U> value{};

  friend bool operator==(const GuidPrefix&, const GuidPrefix&) = default;
};

struct EntityId final {
  std::array<std::uint8_t, 4U> value{};

  friend bool operator==(const EntityId&, const EntityId&) = default;
};

struct DataSubmessage final {
  EntityId reader_id{};
  EntityId writer_id{};
  std::uint64_t writer_sequence_number{0U};
  std::vector<std::uint8_t> serialized_payload;

  friend bool operator==(const DataSubmessage&, const DataSubmessage&) = default;
};

struct HeartbeatSubmessage final {
  EntityId reader_id{};
  EntityId writer_id{};
  std::uint64_t first_sequence_number{0U};
  std::uint64_t last_sequence_number{0U};
  std::uint32_t count{0U};
  bool final_flag{false};
  bool liveliness_flag{false};

  friend bool operator==(const HeartbeatSubmessage&, const HeartbeatSubmessage&) = default;
};

struct AckNackSubmessage final {
  EntityId reader_id{};
  EntityId writer_id{};
  std::uint64_t bitmap_base{0U};
  std::vector<std::uint64_t> missing_sequence_numbers;
  std::uint32_t count{0U};
  bool final_flag{false};

  friend bool operator==(const AckNackSubmessage&, const AckNackSubmessage&) = default;
};

struct RtpsMessage final {
  VendorId vendor_id{};
  GuidPrefix guid_prefix{};
  std::vector<DataSubmessage> data{};
  std::vector<HeartbeatSubmessage> heartbeats{};
  std::vector<AckNackSubmessage> acknacks{};

  friend bool operator==(const RtpsMessage&, const RtpsMessage&) = default;
};

[[nodiscard]] core::Result<std::vector<std::uint8_t>> SerializeRtpsMessage(
  const RtpsMessage& message);
[[nodiscard]] core::Result<RtpsMessage> DeserializeRtpsMessage(std::span<const std::uint8_t> bytes);

[[nodiscard]] bool FitsUdpPayload(const RtpsMessage& message) noexcept;
[[nodiscard]] const char* ToString(std::uint8_t submessage_kind) noexcept;

}  // namespace openautosar::dds::rtps
