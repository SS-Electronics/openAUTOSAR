// SPDX-License-Identifier: MIT

#include "openautosar/e2e/e2e_protection.h"

#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace openautosar::e2e {
namespace {

constexpr std::array<std::uint8_t, 4U> kMagic{'O', 'A', 'E', '2'};
constexpr std::uint8_t kProfile01Wire{1U};
constexpr std::uint8_t kProfile05Wire{5U};

struct ParseResult final {
  CheckStatus status{CheckStatus::kMalformed};
  FrameHeader header{};
  std::vector<std::uint8_t> payload;
};

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"e2e-protection", message};
}

[[nodiscard]] std::optional<std::uint8_t> ProfileWireValue(Profile profile) noexcept {
  switch (profile) {
    case Profile::kProfile01:
      return kProfile01Wire;
    case Profile::kProfile05:
      return kProfile05Wire;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<Profile> ProfileFromWire(std::uint8_t value) noexcept {
  switch (value) {
    case kProfile01Wire:
      return Profile::kProfile01;
    case kProfile05Wire:
      return Profile::kProfile05;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::uint64_t MaxCounterValue(std::uint8_t counter_bits) noexcept {
  if (counter_bits == 32U) {
    return std::numeric_limits<std::uint32_t>::max();
  }

  return (1ULL << counter_bits) - 1ULL;
}

[[nodiscard]] std::uint64_t CounterModulus(std::uint8_t counter_bits) noexcept {
  return MaxCounterValue(counter_bits) + 1ULL;
}

[[nodiscard]] bool IsSupportedCounterWidth(std::uint8_t counter_bits) noexcept {
  return counter_bits == 4U || counter_bits == 8U || counter_bits == 16U ||
         counter_bits == 32U;
}

void AppendUint16(std::vector<std::uint8_t>& output, std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void AppendUint32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void AppendUint64(std::vector<std::uint8_t>& output, std::uint64_t value) {
  AppendUint32(output, static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
  AppendUint32(output, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
}

[[nodiscard]] std::uint16_t ReadUint16(
  std::span<const std::uint8_t> bytes,
  std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(
    (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
    static_cast<std::uint16_t>(bytes[offset + 1U]));
}

[[nodiscard]] std::uint32_t ReadUint32(
  std::span<const std::uint8_t> bytes,
  std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] std::uint64_t ReadUint64(
  std::span<const std::uint8_t> bytes,
  std::size_t offset) noexcept {
  return (static_cast<std::uint64_t>(ReadUint32(bytes, offset)) << 32U) |
         static_cast<std::uint64_t>(ReadUint32(bytes, offset + 4U));
}

[[nodiscard]] std::vector<std::uint8_t> SerializeFrameWithCrc(
  const FrameHeader& header,
  std::span<const std::uint8_t> payload) {
  std::vector<std::uint8_t> frame;
  frame.reserve(kProtectedFrameHeaderSize + payload.size());
  frame.insert(frame.end(), kMagic.begin(), kMagic.end());
  frame.push_back(kProtectedFrameVersion);
  frame.push_back(ProfileWireValue(header.profile).value_or(0U));
  AppendUint16(frame, 0U);
  AppendUint32(frame, header.data_id);
  AppendUint32(frame, header.counter);
  AppendUint64(frame, header.timestamp_ms);
  AppendUint32(frame, header.payload_length);
  AppendUint32(frame, header.crc32);
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

[[nodiscard]] std::vector<std::uint8_t> CrcMaterial(
  const FrameHeader& header,
  std::span<const std::uint8_t> payload) {
  std::vector<std::uint8_t> frame = SerializeFrameWithCrc(header, payload);
  frame[28U] = 0U;
  frame[29U] = 0U;
  frame[30U] = 0U;
  frame[31U] = 0U;
  return std::vector<std::uint8_t>(frame.begin() + 4, frame.end());
}

[[nodiscard]] std::uint32_t CounterDelta(
  const ProfileConfig& config,
  std::uint32_t previous,
  std::uint32_t current) noexcept {
  if (current >= previous) {
    return current - previous;
  }

  const auto delta = CounterModulus(config.counter_bits) -
                     static_cast<std::uint64_t>(previous) +
                     static_cast<std::uint64_t>(current);
  return static_cast<std::uint32_t>(delta);
}

[[nodiscard]] ParseResult ParseProtectedFrame(std::span<const std::uint8_t> frame) {
  if (frame.size() < kProtectedFrameHeaderSize) {
    return {
      .status = CheckStatus::kMalformed,
      .header = {},
      .payload = {},
    };
  }

  for (std::size_t index{0U}; index < kMagic.size(); ++index) {
    if (frame[index] != kMagic[index]) {
      return {
        .status = CheckStatus::kMalformed,
        .header = {},
        .payload = {},
      };
    }
  }

  if (frame[4U] != kProtectedFrameVersion) {
    return {
      .status = CheckStatus::kMalformed,
      .header = {},
      .payload = {},
    };
  }

  const auto profile = ProfileFromWire(frame[5U]);
  if (!profile.has_value()) {
    return {
      .status = CheckStatus::kWrongProfile,
      .header = {},
      .payload = {},
    };
  }

  if (ReadUint16(frame, 6U) != 0U) {
    return {
      .status = CheckStatus::kMalformed,
      .header = {},
      .payload = {},
    };
  }

  const FrameHeader header{
    .profile = profile.value(),
    .data_id = ReadUint32(frame, 8U),
    .counter = ReadUint32(frame, 12U),
    .timestamp_ms = ReadUint64(frame, 16U),
    .payload_length = ReadUint32(frame, 24U),
    .crc32 = ReadUint32(frame, 28U),
  };

  const auto total_size = static_cast<std::uint64_t>(kProtectedFrameHeaderSize) +
                          static_cast<std::uint64_t>(header.payload_length);
  if (total_size != frame.size()) {
    return {
      .status = CheckStatus::kMalformed,
      .header = header,
      .payload = {},
    };
  }

  return {
    .status = CheckStatus::kOk,
    .header = header,
    .payload = std::vector<std::uint8_t>(
      frame.begin() + static_cast<std::ptrdiff_t>(kProtectedFrameHeaderSize),
      frame.end()),
  };
}

[[nodiscard]] CheckedPayload MakeChecked(
  CheckStatus status,
  bool accepted,
  FrameHeader header,
  std::vector<std::uint8_t> payload,
  DeploymentTrace trace) {
  return {
    .status = status,
    .accepted = accepted,
    .header = header,
    .payload = std::move(payload),
    .trace = std::move(trace),
  };
}

}  // namespace

core::Result<bool> ValidateProfileConfig(const ProfileConfig& config) {
  if (!ProfileWireValue(config.profile).has_value()) {
    return core::Result<bool>::FromError(MakeError("E2E profile is unsupported"));
  }

  if (config.data_id == 0U) {
    return core::Result<bool>::FromError(MakeError("E2E data ID must be non-zero"));
  }

  if (!IsSupportedCounterWidth(config.counter_bits)) {
    return core::Result<bool>::FromError(MakeError("E2E counter width is unsupported"));
  }

  const auto max_counter = MaxCounterValue(config.counter_bits);
  if (config.max_delta_counter == 0U || config.max_delta_counter > max_counter) {
    return core::Result<bool>::FromError(MakeError("E2E max delta counter is invalid"));
  }

  if (config.timeout_ms == 0U) {
    return core::Result<bool>::FromError(MakeError("E2E timeout must be non-zero"));
  }

  if (config.trace.service_instance.empty() || config.trace.event_name.empty()) {
    return core::Result<bool>::FromError(MakeError("E2E trace service or event is missing"));
  }

  if (config.trace.source_model_path.empty() || config.trace.source_model_pointer.empty()) {
    return core::Result<bool>::FromError(MakeError("E2E source model trace is missing"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<std::uint32_t> NextCounter(
  const ProfileConfig& config,
  std::uint32_t previous_counter) {
  const auto valid = ValidateProfileConfig(config);
  if (!valid.HasValue()) {
    return core::Result<std::uint32_t>::FromError(valid.Error());
  }

  const auto max_counter = MaxCounterValue(config.counter_bits);
  if (previous_counter > max_counter) {
    return core::Result<std::uint32_t>::FromError(MakeError("E2E counter is out of range"));
  }

  return core::Result<std::uint32_t>::FromValue(
    static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(previous_counter) + 1ULL) %
      CounterModulus(config.counter_bits)));
}

std::uint32_t CalculateCrc32(std::span<const std::uint8_t> bytes) noexcept {
  std::uint32_t crc{0xFFFFFFFFU};
  for (const auto byte : bytes) {
    crc ^= static_cast<std::uint32_t>(byte);
    for (std::uint8_t bit{0U}; bit < 8U; ++bit) {
      const auto mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }

  return ~crc;
}

core::Result<ProtectedPayload> ProtectPayload(
  const ProfileConfig& config,
  std::span<const std::uint8_t> payload,
  std::uint32_t counter,
  std::uint64_t timestamp_ms,
  FaultInjectionMode fault) {
  const auto valid = ValidateProfileConfig(config);
  if (!valid.HasValue()) {
    return core::Result<ProtectedPayload>::FromError(valid.Error());
  }

  if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return core::Result<ProtectedPayload>::FromError(MakeError("E2E payload is too large"));
  }

  if (counter > MaxCounterValue(config.counter_bits)) {
    return core::Result<ProtectedPayload>::FromError(MakeError("E2E counter is out of range"));
  }

  auto data_id = config.data_id;
  auto effective_counter = counter;
  auto effective_timestamp = timestamp_ms;
  if (fault == FaultInjectionMode::kWrongDataId) {
    data_id ^= 0x00000001U;
  } else if (fault == FaultInjectionMode::kRepeatCounter) {
    effective_counter = counter == 0U
                          ? static_cast<std::uint32_t>(MaxCounterValue(config.counter_bits))
                          : counter - 1U;
  } else if (fault == FaultInjectionMode::kStaleTimestamp) {
    const auto stale_delta = static_cast<std::uint64_t>(config.timeout_ms) + 1ULL;
    effective_timestamp = timestamp_ms > stale_delta ? timestamp_ms - stale_delta : 0U;
  }

  FrameHeader header{
    .profile = config.profile,
    .data_id = data_id,
    .counter = effective_counter,
    .timestamp_ms = effective_timestamp,
    .payload_length = static_cast<std::uint32_t>(payload.size()),
    .crc32 = 0U,
  };
  header.crc32 = CalculateCrc32(CrcMaterial(header, payload));
  std::vector<std::uint8_t> frame = SerializeFrameWithCrc(header, payload);

  if (fault == FaultInjectionMode::kCorruptCrc) {
    frame[31U] ^= 0x01U;
  } else if (fault == FaultInjectionMode::kTruncatePayload &&
             frame.size() > kProtectedFrameHeaderSize) {
    frame.pop_back();
  }

  return core::Result<ProtectedPayload>::FromValue({
    .status = fault == FaultInjectionMode::kNone
                ? CheckStatus::kOk
                : CheckStatus::kFaultInjected,
    .header = header,
    .frame = std::move(frame),
    .trace = config.trace,
  });
}

core::Result<CheckedPayload> CheckPayload(
  const ProfileConfig& config,
  std::span<const std::uint8_t> protected_frame,
  std::uint64_t now_ms,
  ReceiverState* state) {
  const auto valid = ValidateProfileConfig(config);
  if (!valid.HasValue()) {
    return core::Result<CheckedPayload>::FromError(valid.Error());
  }

  auto parsed = ParseProtectedFrame(protected_frame);
  if (parsed.status != CheckStatus::kOk) {
    return core::Result<CheckedPayload>::FromValue(
      MakeChecked(parsed.status, false, parsed.header, {}, config.trace));
  }

  if (parsed.header.profile != config.profile) {
    return core::Result<CheckedPayload>::FromValue(
      MakeChecked(CheckStatus::kWrongProfile, false, parsed.header, {}, config.trace));
  }

  if (parsed.header.data_id != config.data_id) {
    return core::Result<CheckedPayload>::FromValue(
      MakeChecked(CheckStatus::kWrongDataId, false, parsed.header, {}, config.trace));
  }

  if (parsed.header.counter > MaxCounterValue(config.counter_bits)) {
    return core::Result<CheckedPayload>::FromValue(
      MakeChecked(CheckStatus::kWrongSequence, false, parsed.header, {}, config.trace));
  }

  const auto expected_crc = CalculateCrc32(CrcMaterial(parsed.header, parsed.payload));
  if (expected_crc != parsed.header.crc32) {
    return core::Result<CheckedPayload>::FromValue(
      MakeChecked(CheckStatus::kCrcMismatch, false, parsed.header, {}, config.trace));
  }

  const auto age_ms = now_ms > parsed.header.timestamp_ms
                        ? now_ms - parsed.header.timestamp_ms
                        : 0ULL;
  if (age_ms > static_cast<std::uint64_t>(config.timeout_ms)) {
    return core::Result<CheckedPayload>::FromValue(
      MakeChecked(CheckStatus::kTimeout, false, parsed.header, parsed.payload, config.trace));
  }

  if (state != nullptr && state->initialized) {
    if (parsed.header.counter == state->last_counter) {
      if (state->repetition_count != std::numeric_limits<std::uint32_t>::max()) {
        ++state->repetition_count;
      }

      const bool accepted = state->repetition_count <= config.max_repetitions;
      return core::Result<CheckedPayload>::FromValue(MakeChecked(
        CheckStatus::kRepeated,
        accepted,
        parsed.header,
        parsed.payload,
        config.trace));
    }

    const auto delta = CounterDelta(config, state->last_counter, parsed.header.counter);
    if (delta == 0U || delta > config.max_delta_counter) {
      return core::Result<CheckedPayload>::FromValue(MakeChecked(
        CheckStatus::kWrongSequence,
        false,
        parsed.header,
        parsed.payload,
        config.trace));
    }
  }

  if (state != nullptr) {
    state->initialized = true;
    state->last_counter = parsed.header.counter;
    state->last_timestamp_ms = parsed.header.timestamp_ms;
    state->repetition_count = 0U;
  }

  return core::Result<CheckedPayload>::FromValue(
    MakeChecked(CheckStatus::kOk, true, parsed.header, parsed.payload, config.trace));
}

std::string_view ToString(Profile profile) noexcept {
  switch (profile) {
    case Profile::kProfile01:
      return "Profile01";
    case Profile::kProfile05:
      return "Profile05";
  }

  return "Unknown";
}

std::string_view ToString(CheckStatus status) noexcept {
  switch (status) {
    case CheckStatus::kOk:
      return "Ok";
    case CheckStatus::kFaultInjected:
      return "FaultInjected";
    case CheckStatus::kRepeated:
      return "Repeated";
    case CheckStatus::kWrongSequence:
      return "WrongSequence";
    case CheckStatus::kTimeout:
      return "Timeout";
    case CheckStatus::kWrongProfile:
      return "WrongProfile";
    case CheckStatus::kWrongDataId:
      return "WrongDataId";
    case CheckStatus::kCrcMismatch:
      return "CrcMismatch";
    case CheckStatus::kMalformed:
      return "Malformed";
    case CheckStatus::kConfigurationError:
      return "ConfigurationError";
  }

  return "Unknown";
}

std::string_view ToString(FaultInjectionMode mode) noexcept {
  switch (mode) {
    case FaultInjectionMode::kNone:
      return "None";
    case FaultInjectionMode::kCorruptCrc:
      return "CorruptCrc";
    case FaultInjectionMode::kWrongDataId:
      return "WrongDataId";
    case FaultInjectionMode::kRepeatCounter:
      return "RepeatCounter";
    case FaultInjectionMode::kStaleTimestamp:
      return "StaleTimestamp";
    case FaultInjectionMode::kTruncatePayload:
      return "TruncatePayload";
  }

  return "Unknown";
}

}  // namespace openautosar::e2e
