// SPDX-License-Identifier: MIT

#include "openautosar/dds/rtps/rtps_message.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace openautosar::dds::rtps {
namespace {

inline constexpr std::array<std::uint8_t, 4U> kRtpsMagic{'R', 'T', 'P', 'S'};
inline constexpr std::uint16_t kOctetsToInlineQos{16U};
inline constexpr std::size_t kSubmessageHeaderSize{4U};
inline constexpr std::size_t kDataSubmessageBodyHeaderSize{20U};
inline constexpr std::size_t kHeartbeatSubmessageBodySize{28U};
inline constexpr std::size_t kAckNackSubmessageBodyHeaderSize{24U};

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

void WriteEntityId(std::vector<std::uint8_t>& bytes, const EntityId& entity_id) {
  bytes.insert(bytes.end(), entity_id.value.begin(), entity_id.value.end());
}

void WriteSequenceNumber(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  WriteU32Le(bytes, static_cast<std::uint32_t>(value >> 32U));
  WriteU32Le(bytes, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
}

[[nodiscard]] std::uint16_t ReadU16Le(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(bytes[offset]) |
    static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32Le(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(bytes[offset]) |
    static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U |
    static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U |
    static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint64_t ReadSequenceNumber(
  std::span<const std::uint8_t> bytes,
  std::size_t offset) {
  const auto high = ReadU32Le(bytes, offset);
  const auto low = ReadU32Le(bytes, offset + 4U);
  return (static_cast<std::uint64_t>(high) << 32U) | static_cast<std::uint64_t>(low);
}

[[nodiscard]] EntityId ReadEntityId(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return {{
    bytes[offset],
    bytes[offset + 1U],
    bytes[offset + 2U],
    bytes[offset + 3U],
  }};
}

[[nodiscard]] bool IsAllZero(const VendorId& vendor_id) noexcept {
  return vendor_id.value[0U] == 0U && vendor_id.value[1U] == 0U;
}

[[nodiscard]] bool HasAnySubmessage(const RtpsMessage& message) noexcept {
  return !message.data.empty() || !message.heartbeats.empty() || !message.acknacks.empty();
}

struct AckNackBitmapData final {
  std::uint32_t num_bits{0U};
  std::vector<std::uint32_t> words;
};

[[nodiscard]] core::Result<AckNackBitmapData> AckNackBitmapWords(
  const AckNackSubmessage& acknack) {
  if (acknack.bitmap_base == 0U) {
    return core::Result<AckNackBitmapData>::FromError(
      {"dds-rtps", "ACKNACK bitmap base is zero"});
  }

  if (acknack.count == 0U) {
    return core::Result<AckNackBitmapData>::FromError(
      {"dds-rtps", "ACKNACK count is zero"});
  }

  std::vector<std::uint64_t> missing = acknack.missing_sequence_numbers;
  std::sort(missing.begin(), missing.end());
  missing.erase(std::unique(missing.begin(), missing.end()), missing.end());

  std::uint32_t num_bits{0U};
  if (!missing.empty()) {
    const auto last = missing.back();
    if (last < acknack.bitmap_base) {
      return core::Result<AckNackBitmapData>::FromError(
        {"dds-rtps", "ACKNACK missing sequence is before bitmap base"});
    }

    const auto span = last - acknack.bitmap_base + 1U;
    if (span > kMaxAckNackBitmapBits) {
      return core::Result<AckNackBitmapData>::FromError(
        {"dds-rtps", "ACKNACK bitmap exceeds supported range"});
    }
    num_bits = static_cast<std::uint32_t>(span);
  }

  std::vector<std::uint32_t> words((num_bits + 31U) / 32U, 0U);
  for (const auto sequence_number : missing) {
    if (sequence_number < acknack.bitmap_base) {
      return core::Result<AckNackBitmapData>::FromError(
        {"dds-rtps", "ACKNACK missing sequence is before bitmap base"});
    }

    const auto relative = sequence_number - acknack.bitmap_base;
    if (relative >= kMaxAckNackBitmapBits) {
      return core::Result<AckNackBitmapData>::FromError(
        {"dds-rtps", "ACKNACK missing sequence exceeds supported range"});
    }

    const auto word_index = static_cast<std::size_t>(relative / 32U);
    const auto bit_index = static_cast<std::uint32_t>(relative % 32U);
    words[word_index] |= std::uint32_t{1U} << (31U - bit_index);
  }

  return core::Result<AckNackBitmapData>::FromValue({
    .num_bits = num_bits,
    .words = std::move(words),
  });
}

[[nodiscard]] std::size_t SerializedSize(const RtpsMessage& message) noexcept {
  std::size_t size{kRtpsHeaderSize};
  for (const auto& data : message.data) {
    size += kDataSubmessageHeaderSize + data.serialized_payload.size();
  }
  size += message.heartbeats.size() * kHeartbeatSubmessageHeaderSize;
  for (const auto& acknack : message.acknacks) {
    if (acknack.bitmap_base == 0U || acknack.missing_sequence_numbers.empty()) {
      size += kAckNackSubmessageHeaderSize;
      continue;
    }

    const auto max_missing = *std::max_element(
      acknack.missing_sequence_numbers.begin(),
      acknack.missing_sequence_numbers.end());
    if (max_missing < acknack.bitmap_base) {
      size += kAckNackSubmessageHeaderSize;
      continue;
    }

    const auto span = max_missing - acknack.bitmap_base + 1U;
    const auto word_count = static_cast<std::size_t>((span + 31U) / 32U);
    size += kAckNackSubmessageHeaderSize + word_count * sizeof(std::uint32_t);
  }

  return size;
}

}  // namespace

core::Result<std::vector<std::uint8_t>> SerializeRtpsMessage(const RtpsMessage& message) {
  if (!HasAnySubmessage(message)) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      {"dds-rtps", "RTPS message contains no submessages"});
  }

  if (IsAllZero(message.vendor_id)) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      {"dds-rtps", "RTPS vendor id is invalid"});
  }

  if (!FitsUdpPayload(message)) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      {"dds-rtps", "RTPS message exceeds configured UDP payload limit"});
  }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(SerializedSize(message));
  bytes.insert(bytes.end(), kRtpsMagic.begin(), kRtpsMagic.end());
  bytes.push_back(kRtpsVersionMajor);
  bytes.push_back(kRtpsVersionMinor);
  bytes.insert(bytes.end(), message.vendor_id.value.begin(), message.vendor_id.value.end());
  bytes.insert(bytes.end(), message.guid_prefix.value.begin(), message.guid_prefix.value.end());

  for (const auto& data : message.data) {
    if (data.writer_sequence_number == 0U) {
      return core::Result<std::vector<std::uint8_t>>::FromError(
        {"dds-rtps", "DATA sequence number is zero"});
    }

    if (data.serialized_payload.empty()) {
      return core::Result<std::vector<std::uint8_t>>::FromError(
        {"dds-rtps", "DATA serialized payload is empty"});
    }

    const auto submessage_length = kDataSubmessageBodyHeaderSize + data.serialized_payload.size();
    if (submessage_length > std::numeric_limits<std::uint16_t>::max()) {
      return core::Result<std::vector<std::uint8_t>>::FromError(
        {"dds-rtps", "DATA submessage length exceeds 16-bit field"});
    }

    bytes.push_back(kDataSubmessageKind);
    bytes.push_back(kLittleEndianFlag | kDataPayloadFlag);
    WriteU16Le(bytes, static_cast<std::uint16_t>(submessage_length));
    WriteU16Le(bytes, 0U);
    WriteU16Le(bytes, kOctetsToInlineQos);
    WriteEntityId(bytes, data.reader_id);
    WriteEntityId(bytes, data.writer_id);
    WriteSequenceNumber(bytes, data.writer_sequence_number);
    bytes.insert(bytes.end(), data.serialized_payload.begin(), data.serialized_payload.end());
  }

  for (const auto& heartbeat : message.heartbeats) {
    if (heartbeat.first_sequence_number == 0U || heartbeat.last_sequence_number == 0U ||
        heartbeat.first_sequence_number > heartbeat.last_sequence_number) {
      return core::Result<std::vector<std::uint8_t>>::FromError(
        {"dds-rtps", "HEARTBEAT sequence range is invalid"});
    }

    if (heartbeat.count == 0U) {
      return core::Result<std::vector<std::uint8_t>>::FromError(
        {"dds-rtps", "HEARTBEAT count is zero"});
    }

    bytes.push_back(kHeartbeatSubmessageKind);
    bytes.push_back(
      kLittleEndianFlag |
      (heartbeat.final_flag ? kFinalFlag : 0U) |
      (heartbeat.liveliness_flag ? kLivelinessFlag : 0U));
    WriteU16Le(bytes, static_cast<std::uint16_t>(kHeartbeatSubmessageBodySize));
    WriteEntityId(bytes, heartbeat.reader_id);
    WriteEntityId(bytes, heartbeat.writer_id);
    WriteSequenceNumber(bytes, heartbeat.first_sequence_number);
    WriteSequenceNumber(bytes, heartbeat.last_sequence_number);
    WriteU32Le(bytes, heartbeat.count);
  }

  for (const auto& acknack : message.acknacks) {
    auto bitmap = AckNackBitmapWords(acknack);
    if (!bitmap) {
      return core::Result<std::vector<std::uint8_t>>::FromError(bitmap.Error());
    }

    const auto submessage_length = kAckNackSubmessageBodyHeaderSize +
                                   bitmap.Value().words.size() * sizeof(std::uint32_t);
    if (submessage_length > std::numeric_limits<std::uint16_t>::max()) {
      return core::Result<std::vector<std::uint8_t>>::FromError(
        {"dds-rtps", "ACKNACK submessage length exceeds 16-bit field"});
    }

    bytes.push_back(kAckNackSubmessageKind);
    bytes.push_back(kLittleEndianFlag | (acknack.final_flag ? kFinalFlag : 0U));
    WriteU16Le(bytes, static_cast<std::uint16_t>(submessage_length));
    WriteEntityId(bytes, acknack.reader_id);
    WriteEntityId(bytes, acknack.writer_id);
    WriteSequenceNumber(bytes, acknack.bitmap_base);
    WriteU32Le(bytes, bitmap.Value().num_bits);
    for (const auto word : bitmap.Value().words) {
      WriteU32Le(bytes, word);
    }
    WriteU32Le(bytes, acknack.count);
  }

  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(bytes));
}

core::Result<RtpsMessage> DeserializeRtpsMessage(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kRtpsHeaderSize) {
    return core::Result<RtpsMessage>::FromError({"dds-rtps", "RTPS message shorter than header"});
  }

  if (!std::equal(kRtpsMagic.begin(), kRtpsMagic.end(), bytes.begin())) {
    return core::Result<RtpsMessage>::FromError({"dds-rtps", "RTPS magic mismatch"});
  }

  if (bytes[4U] != kRtpsVersionMajor || bytes[5U] != kRtpsVersionMinor) {
    return core::Result<RtpsMessage>::FromError({"dds-rtps", "unsupported RTPS version"});
  }

  RtpsMessage message;
  message.vendor_id.value = {bytes[6U], bytes[7U]};
  if (IsAllZero(message.vendor_id)) {
    return core::Result<RtpsMessage>::FromError({"dds-rtps", "RTPS vendor id is invalid"});
  }

  for (std::size_t index = 0U; index < message.guid_prefix.value.size(); ++index) {
    message.guid_prefix.value[index] = bytes[8U + index];
  }

  std::size_t offset{kRtpsHeaderSize};
  while (offset < bytes.size()) {
    if (bytes.size() - offset < kSubmessageHeaderSize) {
      return core::Result<RtpsMessage>::FromError({"dds-rtps", "truncated RTPS submessage"});
    }

    const auto kind = bytes[offset];
    const auto flags = bytes[offset + 1U];
    if ((flags & kLittleEndianFlag) == 0U) {
      return core::Result<RtpsMessage>::FromError(
        {"dds-rtps", "big-endian RTPS submessages are not supported in the MVP"});
    }

    const auto submessage_length = ReadU16Le(bytes, offset + 2U);
    const auto body_offset = offset + kSubmessageHeaderSize;
    const auto next_offset = body_offset + submessage_length;
    if (next_offset > bytes.size()) {
      return core::Result<RtpsMessage>::FromError({"dds-rtps", "RTPS submessage length mismatch"});
    }

    switch (kind) {
      case kDataSubmessageKind: {
        if ((flags & kDataPayloadFlag) == 0U) {
          return core::Result<RtpsMessage>::FromError(
            {"dds-rtps", "DATA submessage does not contain serialized payload"});
        }

        if (submessage_length <= kDataSubmessageBodyHeaderSize) {
          return core::Result<RtpsMessage>::FromError(
            {"dds-rtps", "DATA submessage payload is empty"});
        }

        if (ReadU16Le(bytes, body_offset) != 0U) {
          return core::Result<RtpsMessage>::FromError(
            {"dds-rtps", "DATA extra flags are non-zero"});
        }

        if (ReadU16Le(bytes, body_offset + 2U) != kOctetsToInlineQos) {
          return core::Result<RtpsMessage>::FromError(
            {"dds-rtps", "DATA inline QoS offset is unsupported"});
        }

        DataSubmessage data;
        data.reader_id = ReadEntityId(bytes, body_offset + 4U);
        data.writer_id = ReadEntityId(bytes, body_offset + 8U);
        data.writer_sequence_number = ReadSequenceNumber(bytes, body_offset + 12U);
        if (data.writer_sequence_number == 0U) {
          return core::Result<RtpsMessage>::FromError({"dds-rtps", "DATA sequence number is zero"});
        }

        data.serialized_payload.assign(
          bytes.begin() + static_cast<std::ptrdiff_t>(body_offset + kDataSubmessageBodyHeaderSize),
          bytes.begin() + static_cast<std::ptrdiff_t>(next_offset));
        message.data.push_back(std::move(data));
        break;
      }
      case kHeartbeatSubmessageKind: {
        if (submessage_length != kHeartbeatSubmessageBodySize) {
          return core::Result<RtpsMessage>::FromError(
            {"dds-rtps", "HEARTBEAT submessage length is invalid"});
        }

        HeartbeatSubmessage heartbeat;
        heartbeat.reader_id = ReadEntityId(bytes, body_offset);
        heartbeat.writer_id = ReadEntityId(bytes, body_offset + 4U);
        heartbeat.first_sequence_number = ReadSequenceNumber(bytes, body_offset + 8U);
        heartbeat.last_sequence_number = ReadSequenceNumber(bytes, body_offset + 16U);
        heartbeat.count = ReadU32Le(bytes, body_offset + 24U);
        heartbeat.final_flag = (flags & kFinalFlag) != 0U;
        heartbeat.liveliness_flag = (flags & kLivelinessFlag) != 0U;
        if (heartbeat.first_sequence_number == 0U || heartbeat.last_sequence_number == 0U ||
            heartbeat.first_sequence_number > heartbeat.last_sequence_number) {
          return core::Result<RtpsMessage>::FromError(
            {"dds-rtps", "HEARTBEAT sequence range is invalid"});
        }
        if (heartbeat.count == 0U) {
          return core::Result<RtpsMessage>::FromError({"dds-rtps", "HEARTBEAT count is zero"});
        }

        message.heartbeats.push_back(heartbeat);
        break;
      }
      case kAckNackSubmessageKind: {
        if (submessage_length < kAckNackSubmessageBodyHeaderSize ||
            ((submessage_length - kAckNackSubmessageBodyHeaderSize) % sizeof(std::uint32_t)) !=
              0U) {
          return core::Result<RtpsMessage>::FromError(
            {"dds-rtps", "ACKNACK submessage length is invalid"});
        }

        AckNackSubmessage acknack;
        acknack.reader_id = ReadEntityId(bytes, body_offset);
        acknack.writer_id = ReadEntityId(bytes, body_offset + 4U);
        acknack.bitmap_base = ReadSequenceNumber(bytes, body_offset + 8U);
        const auto num_bits = ReadU32Le(bytes, body_offset + 16U);
        if (acknack.bitmap_base == 0U || num_bits > kMaxAckNackBitmapBits) {
          return core::Result<RtpsMessage>::FromError(
            {"dds-rtps", "ACKNACK bitmap range is invalid"});
        }

        const auto word_count = static_cast<std::size_t>((num_bits + 31U) / 32U);
        const auto expected_length = kAckNackSubmessageBodyHeaderSize +
                                     word_count * sizeof(std::uint32_t);
        if (submessage_length != expected_length) {
          return core::Result<RtpsMessage>::FromError(
            {"dds-rtps", "ACKNACK bitmap length mismatch"});
        }

        for (std::uint32_t bit = 0U; bit < num_bits; ++bit) {
          const auto word_offset = body_offset + 20U +
                                   static_cast<std::size_t>(bit / 32U) * sizeof(std::uint32_t);
          const auto word = ReadU32Le(bytes, word_offset);
          const auto mask = std::uint32_t{1U} << (31U - (bit % 32U));
          if ((word & mask) != 0U) {
            acknack.missing_sequence_numbers.push_back(acknack.bitmap_base + bit);
          }
        }

        acknack.count = ReadU32Le(
          bytes,
          body_offset + 20U + word_count * sizeof(std::uint32_t));
        acknack.final_flag = (flags & kFinalFlag) != 0U;
        if (acknack.count == 0U) {
          return core::Result<RtpsMessage>::FromError({"dds-rtps", "ACKNACK count is zero"});
        }

        message.acknacks.push_back(std::move(acknack));
        break;
      }
      default:
        return core::Result<RtpsMessage>::FromError(
          {"dds-rtps", "unsupported RTPS submessage kind"});
    }
    offset = next_offset;
  }

  if (!HasAnySubmessage(message)) {
    return core::Result<RtpsMessage>::FromError(
      {"dds-rtps", "RTPS message contains no submessages"});
  }

  return core::Result<RtpsMessage>::FromValue(std::move(message));
}

bool FitsUdpPayload(const RtpsMessage& message) noexcept {
  return SerializedSize(message) <= kMaxUdpPayloadSize;
}

const char* ToString(std::uint8_t submessage_kind) noexcept {
  switch (submessage_kind) {
    case kDataSubmessageKind:
      return "data";
    case kHeartbeatSubmessageKind:
      return "heartbeat";
    case kAckNackSubmessageKind:
      return "acknack";
    default:
      return "unsupported";
  }
}

}  // namespace openautosar::dds::rtps
