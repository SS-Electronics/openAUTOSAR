// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/classic_ultrasonic_pdu.h"

#include <algorithm>

namespace openautosar::virtual_vehicle {
namespace {

inline constexpr std::size_t kVersionOffset{0U};
inline constexpr std::size_t kSensorIdOffset{1U};
inline constexpr std::size_t kDistanceOffset{3U};
inline constexpr std::size_t kQualityOffset{7U};
inline constexpr std::size_t kTimestampOffset{8U};
inline constexpr std::size_t kAliveCounterOffset{16U};
inline constexpr std::size_t kDiagnosticOffset{17U};
inline constexpr std::size_t kCrcOffset{21U};

void WriteU16(ClassicUltrasonicPdu& pdu, std::size_t offset, std::uint16_t value) {
  pdu.at(offset) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  pdu.at(offset + 1U) = static_cast<std::uint8_t>(value & 0xFFU);
}

void WriteU32(ClassicUltrasonicPdu& pdu, std::size_t offset, std::uint32_t value) {
  pdu.at(offset) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
  pdu.at(offset + 1U) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  pdu.at(offset + 2U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  pdu.at(offset + 3U) = static_cast<std::uint8_t>(value & 0xFFU);
}

void WriteU64(ClassicUltrasonicPdu& pdu, std::size_t offset, std::uint64_t value) {
  for (std::size_t index = 0U; index < 8U; ++index) {
    const auto shift = static_cast<unsigned>((7U - index) * 8U);
    pdu.at(offset + index) = static_cast<std::uint8_t>((value >> shift) & 0xFFULL);
  }
}

[[nodiscard]] std::uint16_t ReadU16(std::span<const std::uint8_t> pdu, std::size_t offset) {
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(pdu[offset]) << 8U | static_cast<std::uint16_t>(pdu[offset + 1U]));
}

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> pdu, std::size_t offset) {
  return static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(pdu[offset]) << 24U |
    static_cast<std::uint32_t>(pdu[offset + 1U]) << 16U |
    static_cast<std::uint32_t>(pdu[offset + 2U]) << 8U |
    static_cast<std::uint32_t>(pdu[offset + 3U]));
}

[[nodiscard]] std::uint64_t ReadU64(std::span<const std::uint8_t> pdu, std::size_t offset) {
  std::uint64_t value{0U};
  for (std::size_t index = 0U; index < 8U; ++index) {
    value = static_cast<std::uint64_t>((value << 8U) | pdu[offset + index]);
  }
  return value;
}

[[nodiscard]] bool IsValidQuality(std::uint8_t value) noexcept {
  return value == static_cast<std::uint8_t>(UltrasonicQuality::kValid) ||
         value == static_cast<std::uint8_t>(UltrasonicQuality::kDegraded) ||
         value == static_cast<std::uint8_t>(UltrasonicQuality::kInvalid);
}

}  // namespace

ClassicUltrasonicPdu EncodeClassicUltrasonicPdu(
  const UltrasonicSample& sample,
  UltrasonicFault injected_fault) {
  ClassicUltrasonicPdu pdu{};
  pdu.at(kVersionOffset) = kClassicUltrasonicPduVersion;
  WriteU16(pdu, kSensorIdOffset, sample.sensor_id);
  WriteU32(pdu, kDistanceOffset, sample.distance_mm);
  pdu.at(kQualityOffset) = static_cast<std::uint8_t>(sample.quality);
  WriteU64(pdu, kTimestampOffset, sample.timestamp_ns);
  pdu.at(kAliveCounterOffset) = sample.alive_counter;
  WriteU32(pdu, kDiagnosticOffset, sample.diagnostic_status);

  const auto crc =
    ComputeClassicUltrasonicCrc(std::span<const std::uint8_t>(pdu.data(), kCrcOffset));
  WriteU16(pdu, kCrcOffset, crc);

  if (injected_fault == UltrasonicFault::kCrcError) {
    pdu.at(kClassicUltrasonicPduSize - 1U) ^= 0x5AU;
  }

  return pdu;
}

core::Result<UltrasonicSample> DecodeClassicUltrasonicPdu(std::span<const std::uint8_t> pdu) {
  if (pdu.size() != kClassicUltrasonicPduSize) {
    return core::Result<UltrasonicSample>::FromError(
      {"classic-ultrasonic-pdu", "malformed pdu length"});
  }

  if (pdu[kVersionOffset] != kClassicUltrasonicPduVersion) {
    return core::Result<UltrasonicSample>::FromError(
      {"classic-ultrasonic-pdu", "unsupported pdu version"});
  }

  if (!IsValidQuality(pdu[kQualityOffset])) {
    return core::Result<UltrasonicSample>::FromError(
      {"classic-ultrasonic-pdu", "unsupported sample quality"});
  }

  const auto expected_crc = ComputeClassicUltrasonicCrc(pdu.first(kCrcOffset));
  const auto actual_crc = ReadU16(pdu, kCrcOffset);
  if (expected_crc != actual_crc) {
    return core::Result<UltrasonicSample>::FromError(
      {"classic-ultrasonic-pdu", "crc mismatch"});
  }

  UltrasonicSample sample;
  sample.sensor_id = ReadU16(pdu, kSensorIdOffset);
  sample.distance_mm = ReadU32(pdu, kDistanceOffset);
  sample.quality = static_cast<UltrasonicQuality>(pdu[kQualityOffset]);
  sample.timestamp_ns = ReadU64(pdu, kTimestampOffset);
  sample.alive_counter = pdu[kAliveCounterOffset];
  sample.diagnostic_status = ReadU32(pdu, kDiagnosticOffset);
  return core::Result<UltrasonicSample>::FromValue(sample);
}

std::uint16_t ComputeClassicUltrasonicCrc(std::span<const std::uint8_t> bytes) {
  std::uint16_t crc{0xFFFFU};
  for (const auto byte : bytes) {
    crc = static_cast<std::uint16_t>(crc ^ static_cast<std::uint16_t>(byte << 8U));
    for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U);
      } else {
        crc = static_cast<std::uint16_t>(crc << 1U);
      }
    }
  }
  return crc;
}

}  // namespace openautosar::virtual_vehicle
