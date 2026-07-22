// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::e2e {

inline constexpr std::size_t kProtectedFrameHeaderSize{32U};
inline constexpr std::uint8_t kProtectedFrameVersion{1U};

enum class Profile {
  kProfile01,
  kProfile05,
};

enum class CheckStatus {
  kOk,
  kFaultInjected,
  kRepeated,
  kWrongSequence,
  kTimeout,
  kWrongProfile,
  kWrongDataId,
  kCrcMismatch,
  kMalformed,
  kConfigurationError,
};

enum class FaultInjectionMode {
  kNone,
  kCorruptCrc,
  kWrongDataId,
  kRepeatCounter,
  kStaleTimestamp,
  kTruncatePayload,
};

struct DeploymentTrace final {
  std::string service_instance;
  std::string event_name;
  std::string source_model_path;
  std::string source_model_pointer;
  std::string deployment_ref;

  friend bool operator==(const DeploymentTrace&, const DeploymentTrace&) = default;
};

struct ProfileConfig final {
  Profile profile{Profile::kProfile01};
  std::uint32_t data_id{0U};
  std::uint8_t counter_bits{8U};
  std::uint32_t max_delta_counter{1U};
  std::uint32_t timeout_ms{100U};
  std::uint32_t max_repetitions{0U};
  DeploymentTrace trace{};

  friend bool operator==(const ProfileConfig&, const ProfileConfig&) = default;
};

struct FrameHeader final {
  Profile profile{Profile::kProfile01};
  std::uint32_t data_id{0U};
  std::uint32_t counter{0U};
  std::uint64_t timestamp_ms{0U};
  std::uint32_t payload_length{0U};
  std::uint32_t crc32{0U};

  friend bool operator==(const FrameHeader&, const FrameHeader&) = default;
};

struct ProtectedPayload final {
  CheckStatus status{CheckStatus::kOk};
  FrameHeader header{};
  std::vector<std::uint8_t> frame;
  DeploymentTrace trace{};

  friend bool operator==(const ProtectedPayload&, const ProtectedPayload&) = default;
};

struct ReceiverState final {
  bool initialized{false};
  std::uint32_t last_counter{0U};
  std::uint64_t last_timestamp_ms{0U};
  std::uint32_t repetition_count{0U};
};

struct CheckedPayload final {
  CheckStatus status{CheckStatus::kMalformed};
  bool accepted{false};
  FrameHeader header{};
  std::vector<std::uint8_t> payload;
  DeploymentTrace trace{};

  friend bool operator==(const CheckedPayload&, const CheckedPayload&) = default;
};

[[nodiscard]] core::Result<bool> ValidateProfileConfig(const ProfileConfig& config);
[[nodiscard]] core::Result<std::uint32_t> NextCounter(
  const ProfileConfig& config,
  std::uint32_t previous_counter);

[[nodiscard]] std::uint32_t CalculateCrc32(std::span<const std::uint8_t> bytes) noexcept;

[[nodiscard]] core::Result<ProtectedPayload> ProtectPayload(
  const ProfileConfig& config,
  std::span<const std::uint8_t> payload,
  std::uint32_t counter,
  std::uint64_t timestamp_ms,
  FaultInjectionMode fault = FaultInjectionMode::kNone);

[[nodiscard]] core::Result<CheckedPayload> CheckPayload(
  const ProfileConfig& config,
  std::span<const std::uint8_t> protected_frame,
  std::uint64_t now_ms,
  ReceiverState* state = nullptr);

[[nodiscard]] std::string_view ToString(Profile profile) noexcept;
[[nodiscard]] std::string_view ToString(CheckStatus status) noexcept;
[[nodiscard]] std::string_view ToString(FaultInjectionMode mode) noexcept;

}  // namespace openautosar::e2e
