// SPDX-License-Identifier: MIT

#include "openautosar/dds_binding/dds_binding.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace openautosar::dds_binding {
namespace {

inline constexpr std::array<std::uint8_t, 4U> kSampleEnvelopeMagic{'O', 'A', 'D', 'S'};
inline constexpr std::uint8_t kSampleEnvelopeVersion{1U};
inline constexpr std::size_t kSampleEnvelopeHeaderSize{22U};
inline constexpr std::array<std::uint8_t, 4U> kMethodEnvelopeMagic{'O', 'A', 'D', 'M'};
inline constexpr std::uint8_t kMethodEnvelopeVersion{1U};
inline constexpr std::uint8_t kMethodEnvelopeResponseFlag{0x01U};
inline constexpr std::uint8_t kMethodEnvelopeApplicationErrorFlag{0x02U};
inline constexpr std::uint8_t kMethodEnvelopeNoResponseFlag{0x04U};
inline constexpr std::size_t kMethodEnvelopeHeaderSize{32U};
inline constexpr std::array<std::uint8_t, 4U> kMethodErrorPayloadMagic{'O', 'A', 'E', 'R'};
inline constexpr std::uint8_t kMethodErrorPayloadVersion{1U};
inline constexpr std::size_t kMethodErrorPayloadHeaderSize{16U};
inline constexpr std::array<std::uint8_t, 4U> kFieldEnvelopeMagic{'O', 'A', 'D', 'F'};
inline constexpr std::uint8_t kFieldEnvelopeVersion{1U};
inline constexpr std::size_t kFieldEnvelopeHeaderSize{32U};

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"dds-binding", message};
}

[[nodiscard]] std::uint32_t ToSedpReliability(
  DdsReliabilityPolicy reliability) noexcept {
  if (reliability == DdsReliabilityPolicy::kReliable) {
    return dds::rtps::kSedpReliabilityReliable;
  }

  return dds::rtps::kSedpReliabilityBestEffort;
}

[[nodiscard]] std::uint32_t ToSedpDurability(
  DdsDurabilityPolicy durability) noexcept {
  if (durability == DdsDurabilityPolicy::kTransientLocal) {
    return dds::rtps::kSedpDurabilityTransientLocal;
  }

  return dds::rtps::kSedpDurabilityVolatile;
}

[[nodiscard]] core::Result<DdsReliabilityPolicy> FromSedpReliability(
  std::uint32_t reliability_kind) {
  if (reliability_kind == dds::rtps::kSedpReliabilityBestEffort) {
    return core::Result<DdsReliabilityPolicy>::FromValue(
      DdsReliabilityPolicy::kBestEffort);
  }

  if (reliability_kind == dds::rtps::kSedpReliabilityReliable) {
    return core::Result<DdsReliabilityPolicy>::FromValue(
      DdsReliabilityPolicy::kReliable);
  }

  return core::Result<DdsReliabilityPolicy>::FromError(
    MakeError("DDS discovered endpoint reliability QoS is unsupported"));
}

[[nodiscard]] core::Result<DdsDurabilityPolicy> FromSedpDurability(
  std::uint32_t durability_kind) {
  if (durability_kind == dds::rtps::kSedpDurabilityVolatile) {
    return core::Result<DdsDurabilityPolicy>::FromValue(
      DdsDurabilityPolicy::kVolatile);
  }

  if (durability_kind == dds::rtps::kSedpDurabilityTransientLocal) {
    return core::Result<DdsDurabilityPolicy>::FromValue(
      DdsDurabilityPolicy::kTransientLocal);
  }

  return core::Result<DdsDurabilityPolicy>::FromError(
    MakeError("DDS discovered endpoint durability QoS is unsupported"));
}

[[nodiscard]] core::Result<bool> ValidateQosProfile(const DdsQosProfile& qos) {
  if (qos.reliability != DdsReliabilityPolicy::kBestEffort &&
      qos.reliability != DdsReliabilityPolicy::kReliable) {
    return core::Result<bool>::FromError(
      MakeError("DDS reliability QoS policy is unsupported"));
  }

  if (qos.durability != DdsDurabilityPolicy::kVolatile &&
      qos.durability != DdsDurabilityPolicy::kTransientLocal) {
    return core::Result<bool>::FromError(
      MakeError("DDS durability QoS policy is unsupported"));
  }

  if (qos.history != DdsHistoryPolicy::kKeepLast) {
    return core::Result<bool>::FromError(
      MakeError("DDS history QoS policy is unsupported"));
  }

  if (qos.liveliness != DdsLivelinessPolicy::kAutomatic) {
    return core::Result<bool>::FromError(
      MakeError("DDS liveliness QoS policy is unsupported"));
  }

  if (qos.ownership != DdsOwnershipPolicy::kShared) {
    return core::Result<bool>::FromError(
      MakeError("DDS ownership QoS policy is unsupported"));
  }

  if (qos.history_depth == 0U) {
    return core::Result<bool>::FromError(MakeError("DDS history QoS depth is zero"));
  }

  if (qos.max_cached_samples != 0U && qos.max_cached_samples < qos.history_depth) {
    return core::Result<bool>::FromError(
      MakeError("DDS cached-sample resource limit is below history depth"));
  }

  return core::Result<bool>::FromValue(true);
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

void WriteU64Le(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  WriteU32Le(bytes, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
  WriteU32Le(bytes, static_cast<std::uint32_t>(value >> 32U));
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

[[nodiscard]] std::uint64_t ReadU64Le(
  std::span<const std::uint8_t> bytes,
  std::size_t offset) {
  const auto low = ReadU32Le(bytes, offset);
  const auto high = ReadU32Le(bytes, offset + 4U);
  return static_cast<std::uint64_t>(low) |
         (static_cast<std::uint64_t>(high) << 32U);
}

[[nodiscard]] core::Result<bool> ValidateMapping(const DdsTopicMapping& mapping) {
  if (mapping.ara_service.interface_id == 0U || mapping.ara_service.instance_id == 0U) {
    return core::Result<bool>::FromError(MakeError("ARA service identifier is invalid"));
  }

  if (mapping.ara_service.major_version == 0U) {
    return core::Result<bool>::FromError(MakeError("service major version is invalid"));
  }

  if (mapping.topic_name.empty() || mapping.type_name.empty() || mapping.event_name.empty()) {
    return core::Result<bool>::FromError(MakeError("DDS mapping contains an empty name"));
  }

  if (mapping.topic_name.size() > std::numeric_limits<std::uint16_t>::max() ||
      mapping.type_name.size() > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<bool>::FromError(MakeError("DDS topic or type name is too long"));
  }

  auto qos_validation = ValidateQosProfile(mapping.qos);
  if (!qos_validation) {
    return core::Result<bool>::FromError(qos_validation.Error());
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] bool IsAllZero(const dds::rtps::EntityId& entity_id) noexcept {
  return std::all_of(entity_id.value.begin(), entity_id.value.end(), [](std::uint8_t value) {
    return value == 0U;
  });
}

[[nodiscard]] core::Result<bool> ValidateMethodCommon(const DdsMethodMapping& mapping) {
  if (mapping.ara_service.interface_id == 0U || mapping.ara_service.instance_id == 0U) {
    return core::Result<bool>::FromError(MakeError("ARA service identifier is invalid"));
  }

  if (mapping.ara_service.major_version == 0U) {
    return core::Result<bool>::FromError(MakeError("service major version is invalid"));
  }

  if (mapping.method_name.empty()) {
    return core::Result<bool>::FromError(MakeError("DDS method mapping contains an empty name"));
  }

  if (mapping.method_name.size() > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<bool>::FromError(MakeError("DDS method mapping name is too long"));
  }

  auto qos_validation = ValidateQosProfile(mapping.qos);
  if (!qos_validation) {
    return core::Result<bool>::FromError(qos_validation.Error());
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateMethodRequestMapping(
  const DdsMethodMapping& mapping) {
  auto common = ValidateMethodCommon(mapping);
  if (!common) {
    return common;
  }

  if (mapping.request_topic_name.empty() || mapping.request_type_name.empty()) {
    return core::Result<bool>::FromError(
      MakeError("DDS method request mapping contains an empty name"));
  }

  if (mapping.request_topic_name.size() > std::numeric_limits<std::uint16_t>::max() ||
      mapping.request_type_name.size() > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<bool>::FromError(MakeError("DDS method request mapping name is too long"));
  }

  if (IsAllZero(mapping.request_writer_id) || IsAllZero(mapping.request_reader_id)) {
    return core::Result<bool>::FromError(
      MakeError("DDS method request endpoint id is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateMethodResponseMapping(
  const DdsMethodMapping& mapping) {
  auto common = ValidateMethodCommon(mapping);
  if (!common) {
    return common;
  }

  if (mapping.response_topic_name.empty() || mapping.response_type_name.empty()) {
    return core::Result<bool>::FromError(
      MakeError("DDS method response mapping contains an empty name"));
  }

  if (mapping.response_topic_name.size() > std::numeric_limits<std::uint16_t>::max() ||
      mapping.response_type_name.size() > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<bool>::FromError(
      MakeError("DDS method response mapping name is too long"));
  }

  if (IsAllZero(mapping.response_writer_id) || IsAllZero(mapping.response_reader_id)) {
    return core::Result<bool>::FromError(
      MakeError("DDS method response endpoint id is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateFieldMapping(const DdsFieldMapping& mapping) {
  if (mapping.ara_service.interface_id == 0U || mapping.ara_service.instance_id == 0U) {
    return core::Result<bool>::FromError(MakeError("ARA service identifier is invalid"));
  }

  if (mapping.ara_service.major_version == 0U) {
    return core::Result<bool>::FromError(MakeError("service major version is invalid"));
  }

  if (mapping.field_name.empty() || mapping.topic_name.empty() || mapping.type_name.empty()) {
    return core::Result<bool>::FromError(
      MakeError("DDS field mapping contains an empty name"));
  }

  if (mapping.field_name.size() > std::numeric_limits<std::uint16_t>::max() ||
      mapping.topic_name.size() > std::numeric_limits<std::uint16_t>::max() ||
      mapping.type_name.size() > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<bool>::FromError(MakeError("DDS field mapping name is too long"));
  }

  if (IsAllZero(mapping.writer_id) || IsAllZero(mapping.reader_id)) {
    return core::Result<bool>::FromError(MakeError("DDS field endpoint id is invalid"));
  }

  auto qos_validation = ValidateQosProfile(mapping.qos);
  if (!qos_validation) {
    return core::Result<bool>::FromError(qos_validation.Error());
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>> EncodeSamplePayload(
  const DdsTopicMapping& mapping,
  const com::EventSample& sample) {
  if (sample.service != mapping.ara_service) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("event sample service does not match DDS mapping"));
  }

  if (sample.event_name != mapping.event_name) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("event sample name does not match DDS mapping"));
  }

  if (sample.payload.empty()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("event sample payload is empty"));
  }

  if (sample.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("event sample payload length exceeds 32-bit field"));
  }

  std::vector<std::uint8_t> payload;
  payload.reserve(
    kSampleEnvelopeHeaderSize + mapping.topic_name.size() + mapping.type_name.size() +
    sample.payload.size());
  payload.insert(payload.end(), kSampleEnvelopeMagic.begin(), kSampleEnvelopeMagic.end());
  payload.push_back(kSampleEnvelopeVersion);
  payload.push_back(0U);
  WriteU16Le(payload, static_cast<std::uint16_t>(mapping.topic_name.size()));
  WriteU16Le(payload, static_cast<std::uint16_t>(mapping.type_name.size()));
  WriteU32Le(payload, mapping.ara_service.interface_id);
  WriteU32Le(payload, mapping.ara_service.instance_id);
  WriteU32Le(payload, static_cast<std::uint32_t>(sample.payload.size()));
  payload.insert(payload.end(), mapping.topic_name.begin(), mapping.topic_name.end());
  payload.insert(payload.end(), mapping.type_name.begin(), mapping.type_name.end());
  payload.insert(payload.end(), sample.payload.begin(), sample.payload.end());
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(payload));
}

[[nodiscard]] core::Result<com::EventSample> DecodeSamplePayload(
  const DdsTopicMapping& mapping,
  std::span<const std::uint8_t> payload) {
  if (payload.size() < kSampleEnvelopeHeaderSize) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS serialized payload is shorter than envelope header"));
  }

  if (!std::equal(kSampleEnvelopeMagic.begin(), kSampleEnvelopeMagic.end(), payload.begin())) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS serialized payload magic mismatch"));
  }

  if (payload[4U] != kSampleEnvelopeVersion || payload[5U] != 0U) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS serialized payload version is unsupported"));
  }

  const auto topic_length = ReadU16Le(payload, 6U);
  const auto type_length = ReadU16Le(payload, 8U);
  const auto interface_id = ReadU32Le(payload, 10U);
  const auto instance_id = ReadU32Le(payload, 14U);
  const auto sample_length = ReadU32Le(payload, 18U);
  const auto expected_size = kSampleEnvelopeHeaderSize + topic_length + type_length +
                             static_cast<std::size_t>(sample_length);
  if (payload.size() != expected_size) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS serialized payload length mismatch"));
  }

  if (interface_id != mapping.ara_service.interface_id ||
      instance_id != mapping.ara_service.instance_id) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS serialized payload service id mismatch"));
  }

  const auto topic_begin = payload.begin() + static_cast<std::ptrdiff_t>(kSampleEnvelopeHeaderSize);
  const auto type_begin = topic_begin + topic_length;
  const auto sample_begin = type_begin + type_length;
  const std::string topic(topic_begin, type_begin);
  const std::string type(type_begin, sample_begin);

  if (topic != mapping.topic_name || type != mapping.type_name) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS topic or type name does not match mapping"));
  }

  std::vector<std::uint8_t> sample_payload(sample_begin, payload.end());
  if (sample_payload.empty()) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS serialized event payload is empty"));
  }

  return core::Result<com::EventSample>::FromValue({
    .service = mapping.ara_service,
    .event_name = mapping.event_name,
    .payload = std::move(sample_payload),
    .sequence = 0U,
  });
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>> EncodeFieldPayload(
  const DdsFieldMapping& mapping,
  const com::FieldValue& value) {
  if (value.service != mapping.ara_service) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("field value service does not match DDS mapping"));
  }

  if (value.field_name != mapping.field_name) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("field value name does not match DDS mapping"));
  }

  if (value.payload.empty()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("field value payload is empty"));
  }

  if (value.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("DDS field payload length exceeds 32-bit field"));
  }

  std::vector<std::uint8_t> payload;
  payload.reserve(
    kFieldEnvelopeHeaderSize + mapping.topic_name.size() + mapping.type_name.size() +
    mapping.field_name.size() + value.payload.size());
  payload.insert(payload.end(), kFieldEnvelopeMagic.begin(), kFieldEnvelopeMagic.end());
  payload.push_back(kFieldEnvelopeVersion);
  payload.push_back(0U);
  WriteU16Le(payload, static_cast<std::uint16_t>(mapping.topic_name.size()));
  WriteU16Le(payload, static_cast<std::uint16_t>(mapping.type_name.size()));
  WriteU16Le(payload, static_cast<std::uint16_t>(mapping.field_name.size()));
  WriteU32Le(payload, mapping.ara_service.interface_id);
  WriteU32Le(payload, mapping.ara_service.instance_id);
  WriteU64Le(payload, value.sequence);
  WriteU32Le(payload, static_cast<std::uint32_t>(value.payload.size()));
  payload.insert(payload.end(), mapping.topic_name.begin(), mapping.topic_name.end());
  payload.insert(payload.end(), mapping.type_name.begin(), mapping.type_name.end());
  payload.insert(payload.end(), mapping.field_name.begin(), mapping.field_name.end());
  payload.insert(payload.end(), value.payload.begin(), value.payload.end());
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(payload));
}

[[nodiscard]] core::Result<com::FieldValue> DecodeFieldPayload(
  const DdsFieldMapping& mapping,
  std::span<const std::uint8_t> payload) {
  if (payload.size() < kFieldEnvelopeHeaderSize) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("DDS field payload is shorter than envelope header"));
  }

  if (!std::equal(kFieldEnvelopeMagic.begin(), kFieldEnvelopeMagic.end(), payload.begin())) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("DDS field payload magic mismatch"));
  }

  if (payload[4U] != kFieldEnvelopeVersion || payload[5U] != 0U) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("DDS field payload version is unsupported"));
  }

  const auto topic_length = ReadU16Le(payload, 6U);
  const auto type_length = ReadU16Le(payload, 8U);
  const auto field_length = ReadU16Le(payload, 10U);
  const auto interface_id = ReadU32Le(payload, 12U);
  const auto instance_id = ReadU32Le(payload, 16U);
  const auto field_sequence = ReadU64Le(payload, 20U);
  const auto field_payload_length = ReadU32Le(payload, 28U);
  const auto expected_size = kFieldEnvelopeHeaderSize + topic_length + type_length +
                             field_length +
                             static_cast<std::size_t>(field_payload_length);
  if (payload.size() != expected_size) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("DDS field payload length mismatch"));
  }

  if (interface_id != mapping.ara_service.interface_id ||
      instance_id != mapping.ara_service.instance_id) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("DDS field payload service id mismatch"));
  }

  const auto topic_begin =
    payload.begin() + static_cast<std::ptrdiff_t>(kFieldEnvelopeHeaderSize);
  const auto type_begin = topic_begin + topic_length;
  const auto field_begin = type_begin + type_length;
  const auto field_payload_begin = field_begin + field_length;
  const std::string topic(topic_begin, type_begin);
  const std::string type(type_begin, field_begin);
  const std::string field(field_begin, field_payload_begin);

  if (topic != mapping.topic_name || type != mapping.type_name ||
      field != mapping.field_name) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("DDS field topic, type, or name does not match mapping"));
  }

  std::vector<std::uint8_t> field_payload(field_payload_begin, payload.end());
  if (field_payload.empty()) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("DDS serialized field payload is empty"));
  }

  return core::Result<com::FieldValue>::FromValue({
    .service = mapping.ara_service,
    .field_name = mapping.field_name,
    .payload = std::move(field_payload),
    .sequence = field_sequence,
  });
}

struct DecodedMethodPayload final {
  std::vector<std::uint8_t> payload;
  std::uint64_t correlation_id{0U};
  bool expects_response{true};
  bool application_error{false};
  std::string error_domain;
  std::uint32_t error_code{0U};
};

struct MethodErrorPayload final {
  std::vector<std::uint8_t> payload;
  std::string error_domain;
  std::uint32_t error_code{0U};
  bool structured{false};
};

[[nodiscard]] bool HasMethodErrorPayloadMagic(std::span<const std::uint8_t> payload) {
  return payload.size() >= kMethodErrorPayloadMagic.size() &&
         std::equal(
           kMethodErrorPayloadMagic.begin(),
           kMethodErrorPayloadMagic.end(),
           payload.begin());
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>> EncodeMethodErrorPayload(
  std::span<const std::uint8_t> method_payload,
  bool application_error,
  std::string_view error_domain,
  std::uint32_t error_code) {
  if (!application_error && method_payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("DDS method payload length exceeds 32-bit field"));
  }

  if (!application_error || (error_domain.empty() && error_code == 0U)) {
    return core::Result<std::vector<std::uint8_t>>::FromValue(
      std::vector<std::uint8_t>(method_payload.begin(), method_payload.end()));
  }

  if (error_domain.empty()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("DDS method error domain is empty"));
  }

  if (error_domain.size() > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("DDS method error domain is too long"));
  }

  if (method_payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("DDS method error payload length exceeds 32-bit field"));
  }

  std::vector<std::uint8_t> payload;
  payload.reserve(kMethodErrorPayloadHeaderSize + error_domain.size() + method_payload.size());
  payload.insert(
    payload.end(),
    kMethodErrorPayloadMagic.begin(),
    kMethodErrorPayloadMagic.end());
  payload.push_back(kMethodErrorPayloadVersion);
  payload.push_back(0U);
  WriteU16Le(payload, static_cast<std::uint16_t>(error_domain.size()));
  WriteU32Le(payload, error_code);
  WriteU32Le(payload, static_cast<std::uint32_t>(method_payload.size()));
  payload.insert(payload.end(), error_domain.begin(), error_domain.end());
  payload.insert(payload.end(), method_payload.begin(), method_payload.end());
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(payload));
}

[[nodiscard]] core::Result<MethodErrorPayload> DecodeMethodErrorPayload(
  std::span<const std::uint8_t> payload) {
  if (!HasMethodErrorPayloadMagic(payload)) {
    return core::Result<MethodErrorPayload>::FromValue({
      .payload = std::vector<std::uint8_t>(payload.begin(), payload.end()),
      .error_domain = {},
      .error_code = 0U,
      .structured = false,
    });
  }

  if (payload.size() < kMethodErrorPayloadHeaderSize) {
    return core::Result<MethodErrorPayload>::FromError(
      MakeError("DDS method error payload is shorter than envelope header"));
  }

  if (payload[4U] != kMethodErrorPayloadVersion || payload[5U] != 0U) {
    return core::Result<MethodErrorPayload>::FromError(
      MakeError("DDS method error payload version is unsupported"));
  }

  const auto domain_length = ReadU16Le(payload, 6U);
  const auto error_code = ReadU32Le(payload, 8U);
  const auto value_length = ReadU32Le(payload, 12U);
  const auto expected_size = kMethodErrorPayloadHeaderSize + domain_length +
                             static_cast<std::size_t>(value_length);
  if (payload.size() != expected_size) {
    return core::Result<MethodErrorPayload>::FromError(
      MakeError("DDS method error payload length mismatch"));
  }

  const auto domain_begin =
    payload.begin() + static_cast<std::ptrdiff_t>(kMethodErrorPayloadHeaderSize);
  const auto value_begin = domain_begin + domain_length;
  return core::Result<MethodErrorPayload>::FromValue({
    .payload = std::vector<std::uint8_t>(value_begin, payload.end()),
    .error_domain = std::string(domain_begin, value_begin),
    .error_code = error_code,
    .structured = true,
  });
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>> EncodeMethodPayload(
  const DdsMethodMapping& mapping,
  std::span<const std::uint8_t> method_payload,
  std::uint64_t correlation_id,
  bool response,
  bool expects_response,
  bool application_error,
  std::string_view error_domain = {},
  std::uint32_t error_code = 0U) {
  if (correlation_id == 0U) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("DDS method correlation id is zero"));
  }

  auto encoded_method_payload = EncodeMethodErrorPayload(
    method_payload,
    application_error,
    error_domain,
    error_code);
  if (!encoded_method_payload) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      encoded_method_payload.Error());
  }
  auto encoded_method_payload_value = std::move(encoded_method_payload.Value());

  const std::string& topic = response ? mapping.response_topic_name
                                      : mapping.request_topic_name;
  const std::string& type = response ? mapping.response_type_name
                                     : mapping.request_type_name;
  std::vector<std::uint8_t> payload;
  payload.reserve(
    kMethodEnvelopeHeaderSize + topic.size() + type.size() + mapping.method_name.size() +
    encoded_method_payload_value.size());
  payload.insert(payload.end(), kMethodEnvelopeMagic.begin(), kMethodEnvelopeMagic.end());
  payload.push_back(kMethodEnvelopeVersion);
  payload.push_back(
    static_cast<std::uint8_t>(
      (response ? kMethodEnvelopeResponseFlag : 0U) |
      (!expects_response ? kMethodEnvelopeNoResponseFlag : 0U) |
      (application_error ? kMethodEnvelopeApplicationErrorFlag : 0U)));
  WriteU16Le(payload, static_cast<std::uint16_t>(topic.size()));
  WriteU16Le(payload, static_cast<std::uint16_t>(type.size()));
  WriteU16Le(payload, static_cast<std::uint16_t>(mapping.method_name.size()));
  WriteU32Le(payload, mapping.ara_service.interface_id);
  WriteU32Le(payload, mapping.ara_service.instance_id);
  WriteU64Le(payload, correlation_id);
  WriteU32Le(payload, static_cast<std::uint32_t>(encoded_method_payload_value.size()));
  payload.insert(payload.end(), topic.begin(), topic.end());
  payload.insert(payload.end(), type.begin(), type.end());
  payload.insert(payload.end(), mapping.method_name.begin(), mapping.method_name.end());
  payload.insert(
    payload.end(),
    encoded_method_payload_value.begin(),
    encoded_method_payload_value.end());
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(payload));
}

[[nodiscard]] core::Result<DecodedMethodPayload> DecodeMethodPayload(
  const DdsMethodMapping& mapping,
  std::span<const std::uint8_t> payload,
  bool response) {
  if (payload.size() < kMethodEnvelopeHeaderSize) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method payload is shorter than envelope header"));
  }

  if (!std::equal(kMethodEnvelopeMagic.begin(), kMethodEnvelopeMagic.end(), payload.begin())) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method payload magic mismatch"));
  }

  if (payload[4U] != kMethodEnvelopeVersion) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method payload version is unsupported"));
  }

  const bool payload_is_response = (payload[5U] & kMethodEnvelopeResponseFlag) != 0U;
  const bool payload_expects_response = (payload[5U] & kMethodEnvelopeNoResponseFlag) == 0U;
  const bool application_error =
    (payload[5U] & kMethodEnvelopeApplicationErrorFlag) != 0U;
  if (payload_is_response != response) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method payload direction does not match mapping"));
  }

  if (response && !payload_expects_response) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method response payload carries a no-response flag"));
  }

  if (!response && application_error) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method request payload carries an error flag"));
  }

  const auto topic_length = ReadU16Le(payload, 6U);
  const auto type_length = ReadU16Le(payload, 8U);
  const auto method_length = ReadU16Le(payload, 10U);
  const auto interface_id = ReadU32Le(payload, 12U);
  const auto instance_id = ReadU32Le(payload, 16U);
  const auto correlation_id = ReadU64Le(payload, 20U);
  const auto method_payload_length = ReadU32Le(payload, 28U);
  const auto expected_size = kMethodEnvelopeHeaderSize + topic_length + type_length +
                             method_length +
                             static_cast<std::size_t>(method_payload_length);
  if (payload.size() != expected_size) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method payload length mismatch"));
  }

  if (interface_id != mapping.ara_service.interface_id ||
      instance_id != mapping.ara_service.instance_id) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method payload service id mismatch"));
  }

  if (correlation_id == 0U) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method payload correlation id is zero"));
  }

  const auto topic_begin =
    payload.begin() + static_cast<std::ptrdiff_t>(kMethodEnvelopeHeaderSize);
  const auto type_begin = topic_begin + topic_length;
  const auto method_begin = type_begin + type_length;
  const auto method_payload_begin = method_begin + method_length;
  const std::string topic(topic_begin, type_begin);
  const std::string type(type_begin, method_begin);
  const std::string method(method_begin, method_payload_begin);
  const std::string& expected_topic = response ? mapping.response_topic_name
                                               : mapping.request_topic_name;
  const std::string& expected_type = response ? mapping.response_type_name
                                              : mapping.request_type_name;

  if (topic != expected_topic || type != expected_type || method != mapping.method_name) {
    return core::Result<DecodedMethodPayload>::FromError(
      MakeError("DDS method topic, type, or name does not match mapping"));
  }

  std::vector<std::uint8_t> raw_method_payload(method_payload_begin, payload.end());
  MethodErrorPayload decoded_error{
    .payload = raw_method_payload,
    .error_domain = {},
    .error_code = 0U,
    .structured = false,
  };
  if (response && application_error) {
    auto decoded = DecodeMethodErrorPayload(raw_method_payload);
    if (!decoded) {
      return core::Result<DecodedMethodPayload>::FromError(decoded.Error());
    }
    decoded_error = std::move(decoded.Value());
  }

  return core::Result<DecodedMethodPayload>::FromValue({
    .payload = std::move(decoded_error.payload),
    .correlation_id = correlation_id,
    .expects_response = payload_expects_response,
    .application_error = application_error,
    .error_domain = std::move(decoded_error.error_domain),
    .error_code = decoded_error.error_code,
  });
}

[[nodiscard]] core::Result<dds::rtps::RtpsMessage> BuildDataMessageForReader(
  const DdsTopicMapping& mapping,
  const dds::rtps::EntityId& reader_id,
  const com::EventSample& sample,
  std::uint64_t sequence_number) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(validation.Error());
  }

  if (IsAllZero(reader_id)) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS DATA reader id is invalid"));
  }

  if (sequence_number == 0U) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS writer sequence number is zero"));
  }

  auto serialized_payload = EncodeSamplePayload(mapping, sample);
  if (!serialized_payload) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(serialized_payload.Error());
  }

  dds::rtps::RtpsMessage message{
    .vendor_id = {},
    .guid_prefix = mapping.participant_guid_prefix,
    .data = {{
      .reader_id = reader_id,
      .writer_id = mapping.writer_id,
      .writer_sequence_number = sequence_number,
      .serialized_payload = std::move(serialized_payload.Value()),
    }},
  };

  if (!dds::rtps::FitsUdpPayload(message)) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS RTPS event exceeds configured UDP payload limit"));
  }

  return core::Result<dds::rtps::RtpsMessage>::FromValue(std::move(message));
}

[[nodiscard]] core::Result<dds::rtps::RtpsMessage> BuildMethodDataMessage(
  const DdsMethodMapping& mapping,
  const dds::rtps::EntityId& writer_id,
  const dds::rtps::EntityId& reader_id,
  std::vector<std::uint8_t> serialized_payload,
  std::uint64_t sequence_number,
  const char* overflow_error_message) {
  if (IsAllZero(writer_id) || IsAllZero(reader_id)) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS method DATA endpoint id is invalid"));
  }

  if (sequence_number == 0U) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS method DATA sequence number is zero"));
  }

  dds::rtps::RtpsMessage message{
    .vendor_id = {},
    .guid_prefix = mapping.participant_guid_prefix,
    .data = {{
      .reader_id = reader_id,
      .writer_id = writer_id,
      .writer_sequence_number = sequence_number,
      .serialized_payload = std::move(serialized_payload),
    }},
  };

  if (!dds::rtps::FitsUdpPayload(message)) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError(overflow_error_message));
  }

  return core::Result<dds::rtps::RtpsMessage>::FromValue(std::move(message));
}

[[nodiscard]] core::Result<const dds::rtps::DataSubmessage*> FindMethodDataSubmessage(
  const dds::rtps::RtpsMessage& message,
  const dds::rtps::EntityId& writer_id,
  const dds::rtps::EntityId& reader_id) {
  for (const auto& data : message.data) {
    if (data.writer_id != writer_id || data.reader_id != reader_id) {
      continue;
    }

    if (data.writer_sequence_number == 0U) {
      return core::Result<const dds::rtps::DataSubmessage*>::FromError(
        MakeError("DDS method DATA sequence number is zero"));
    }

    return core::Result<const dds::rtps::DataSubmessage*>::FromValue(&data);
  }

  return core::Result<const dds::rtps::DataSubmessage*>::FromError(
    MakeError("DDS RTPS message does not contain the mapped method DATA pair"));
}

[[nodiscard]] dds::rtps::SedpEndpointKind ToSedpKind(DdsEndpointRole role) noexcept {
  if (role == DdsEndpointRole::kPublication) {
    return dds::rtps::SedpEndpointKind::kPublication;
  }

  return dds::rtps::SedpEndpointKind::kSubscription;
}

[[nodiscard]] dds::rtps::EntityId EndpointIdForRole(
  const DdsTopicMapping& mapping,
  DdsEndpointRole role) noexcept {
  if (role == DdsEndpointRole::kPublication) {
    return mapping.writer_id;
  }

  return mapping.reader_id;
}

[[nodiscard]] dds::rtps::EntityId EndpointIdForRole(
  const DdsFieldMapping& mapping,
  DdsEndpointRole role) noexcept {
  if (role == DdsEndpointRole::kPublication) {
    return mapping.writer_id;
  }

  return mapping.reader_id;
}

[[nodiscard]] DdsEndpointRole OppositeRole(DdsEndpointRole role) noexcept {
  if (role == DdsEndpointRole::kPublication) {
    return DdsEndpointRole::kSubscription;
  }

  return DdsEndpointRole::kPublication;
}

[[nodiscard]] std::pair<dds::rtps::EntityId, dds::rtps::EntityId> SedpBuiltinPairForRole(
  DdsEndpointRole role) noexcept {
  if (role == DdsEndpointRole::kPublication) {
    return {
      dds::rtps::kSedpBuiltinPublicationsReaderId,
      dds::rtps::kSedpBuiltinPublicationsWriterId,
    };
  }

  return {
    dds::rtps::kSedpBuiltinSubscriptionsReaderId,
    dds::rtps::kSedpBuiltinSubscriptionsWriterId,
  };
}

[[nodiscard]] core::Result<std::uint64_t> FindSedpSequenceNumber(
  DdsEndpointRole role,
  const dds::rtps::RtpsMessage& message) {
  const auto [reader_id, writer_id] = SedpBuiltinPairForRole(role);
  for (const auto& data : message.data) {
    if (data.writer_id != writer_id || data.reader_id != reader_id) {
      continue;
    }

    if (data.writer_sequence_number == 0U) {
      return core::Result<std::uint64_t>::FromError(
        MakeError("DDS SEDP sequence number is zero"));
    }

    return core::Result<std::uint64_t>::FromValue(data.writer_sequence_number);
  }

  return core::Result<std::uint64_t>::FromError(
    MakeError("DDS SEDP DATA submessage for the requested role is missing"));
}

[[nodiscard]] bool SameEndpointIdentity(
  const DdsDiscoveredEndpoint& left,
  const DdsDiscoveredEndpoint& right) noexcept {
  return left.role == right.role &&
         left.participant_guid_prefix == right.participant_guid_prefix &&
         left.endpoint_id == right.endpoint_id;
}

[[nodiscard]] std::uint64_t AgeMs(std::uint64_t now_ms, std::uint64_t last_seen_ms) noexcept {
  if (now_ms < last_seen_ms) {
    return 0U;
  }

  return now_ms - last_seen_ms;
}

[[nodiscard]] bool IsExpired(
  const DdsDiscoveredEndpoint& endpoint,
  std::uint64_t now_ms,
  std::uint64_t endpoint_ttl_ms) noexcept {
  return AgeMs(now_ms, endpoint.last_seen_ms) > endpoint_ttl_ms;
}

[[nodiscard]] core::Result<bool> HasStaticQosProfileMatch(
  const DdsQosProfile& local_qos,
  const DdsDiscoveredEndpoint& remote_endpoint) {
  auto remote_reliability = FromSedpReliability(remote_endpoint.reliability_kind);
  if (!remote_reliability) {
    return core::Result<bool>::FromError(remote_reliability.Error());
  }

  auto remote_durability = FromSedpDurability(remote_endpoint.durability_kind);
  if (!remote_durability) {
    return core::Result<bool>::FromError(remote_durability.Error());
  }

  return core::Result<bool>::FromValue(
    local_qos.reliability == remote_reliability.Value() &&
    local_qos.durability == remote_durability.Value());
}

[[nodiscard]] core::Result<DdsEndpointMatch> BuildEndpointMatch(
  const DdsTopicMapping& local_mapping,
  DdsEndpointRole local_role,
  const DdsDiscoveredEndpoint& remote_endpoint) {
  if (remote_endpoint.role != OppositeRole(local_role)) {
    return core::Result<DdsEndpointMatch>::FromError(
      MakeError("DDS discovered endpoint role is not compatible"));
  }

  if (remote_endpoint.participant_guid_prefix == local_mapping.participant_guid_prefix) {
    return core::Result<DdsEndpointMatch>::FromError(
      MakeError("DDS discovered endpoint belongs to the local participant"));
  }

  if (remote_endpoint.topic_name != local_mapping.topic_name ||
      remote_endpoint.type_name != local_mapping.type_name) {
    return core::Result<DdsEndpointMatch>::FromError(
      MakeError("DDS discovered endpoint topic or type does not match"));
  }

  auto qos_match = HasStaticQosProfileMatch(local_mapping.qos, remote_endpoint);
  if (!qos_match) {
    return core::Result<DdsEndpointMatch>::FromError(qos_match.Error());
  }

  if (!qos_match.Value()) {
    return core::Result<DdsEndpointMatch>::FromError(
      MakeError("DDS discovered endpoint QoS does not match the local static profile"));
  }

  return core::Result<DdsEndpointMatch>::FromValue({
    .local_role = local_role,
    .local_endpoint_id = EndpointIdForRole(local_mapping, local_role),
    .remote_endpoint = remote_endpoint,
  });
}

[[nodiscard]] core::Result<bool> ValidateEndpointMatch(
  const DdsTopicMapping& mapping,
  const DdsEndpointMatch& match,
  DdsEndpointRole local_role) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<bool>::FromError(validation.Error());
  }

  if (match.local_role != local_role) {
    return core::Result<bool>::FromError(
      MakeError("DDS endpoint match has the wrong local role"));
  }

  if (match.local_endpoint_id != EndpointIdForRole(mapping, local_role)) {
    return core::Result<bool>::FromError(
      MakeError("DDS endpoint match has the wrong local endpoint id"));
  }

  if (IsAllZero(match.remote_endpoint.endpoint_id)) {
    return core::Result<bool>::FromError(
      MakeError("DDS endpoint match has an invalid remote endpoint id"));
  }

  auto rebuilt = BuildEndpointMatch(mapping, local_role, match.remote_endpoint);
  if (!rebuilt) {
    return core::Result<bool>::FromError(rebuilt.Error());
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<const dds::rtps::DataSubmessage*> FindMatchedDataSubmessage(
  const DdsEndpointMatch& match,
  const dds::rtps::RtpsMessage& message) {
  for (const auto& data : message.data) {
    if (data.writer_id != match.remote_endpoint.endpoint_id ||
        data.reader_id != match.local_endpoint_id) {
      continue;
    }

    if (data.writer_sequence_number == 0U) {
      return core::Result<const dds::rtps::DataSubmessage*>::FromError(
        MakeError("DDS DATA writer sequence number is zero"));
    }

    return core::Result<const dds::rtps::DataSubmessage*>::FromValue(&data);
  }

  return core::Result<const dds::rtps::DataSubmessage*>::FromError(
    MakeError("DDS RTPS message does not contain the matched writer/reader pair"));
}

[[nodiscard]] core::Result<const dds::rtps::HeartbeatSubmessage*> FindMatchedHeartbeat(
  const DdsEndpointMatch& match,
  const dds::rtps::RtpsMessage& message) {
  for (const auto& heartbeat : message.heartbeats) {
    if (heartbeat.writer_id != match.remote_endpoint.endpoint_id ||
        heartbeat.reader_id != match.local_endpoint_id) {
      continue;
    }

    return core::Result<const dds::rtps::HeartbeatSubmessage*>::FromValue(&heartbeat);
  }

  return core::Result<const dds::rtps::HeartbeatSubmessage*>::FromError(
    MakeError("DDS RTPS message does not contain the matched HEARTBEAT"));
}

[[nodiscard]] core::Result<const dds::rtps::AckNackSubmessage*> FindMatchedAckNack(
  const DdsEndpointMatch& match,
  const dds::rtps::RtpsMessage& message) {
  for (const auto& acknack : message.acknacks) {
    if (acknack.reader_id != match.remote_endpoint.endpoint_id ||
        acknack.writer_id != match.local_endpoint_id) {
      continue;
    }

    return core::Result<const dds::rtps::AckNackSubmessage*>::FromValue(&acknack);
  }

  return core::Result<const dds::rtps::AckNackSubmessage*>::FromError(
    MakeError("DDS RTPS message does not contain the matched ACKNACK"));
}

[[nodiscard]] bool ContainsSequence(
  const std::vector<std::uint64_t>& sequences,
  std::uint64_t sequence_number) noexcept {
  return std::find(sequences.begin(), sequences.end(), sequence_number) != sequences.end();
}

[[nodiscard]] bool IsLimitEnabled(std::size_t limit) noexcept {
  return limit != 0U;
}

[[nodiscard]] bool ExceedsDuration(
  std::uint64_t now_ms,
  std::uint64_t last_seen_ms,
  std::uint64_t duration_ms) noexcept {
  return duration_ms != 0U && AgeMs(now_ms, last_seen_ms) > duration_ms;
}

[[nodiscard]] std::vector<DdsReaderSequenceState>::iterator FindWriterState(
  std::vector<DdsReaderSequenceState>& states,
  const DdsEndpointMatch& match) {
  return std::find_if(
    states.begin(),
    states.end(),
    [&match](const DdsReaderSequenceState& state) {
      return state.participant_guid_prefix == match.remote_endpoint.participant_guid_prefix &&
             state.writer_id == match.remote_endpoint.endpoint_id;
    });
}

[[nodiscard]] std::size_t RemoveExpiredWriterStates(
  std::vector<DdsReaderSequenceState>& writer_states,
  std::uint64_t now_ms,
  std::uint64_t liveliness_lease_duration_ms) {
  if (liveliness_lease_duration_ms == 0U) {
    return 0U;
  }

  const auto original_size = writer_states.size();
  writer_states.erase(
    std::remove_if(
      writer_states.begin(),
      writer_states.end(),
      [now_ms, liveliness_lease_duration_ms](const DdsReaderSequenceState& state) {
        return ExceedsDuration(now_ms, state.last_seen_ms, liveliness_lease_duration_ms);
      }),
    writer_states.end());
  return original_size - writer_states.size();
}

[[nodiscard]] core::Result<bool> ValidateTransportClassProfile(
  const DdsTopicMapping& mapping,
  DdsReliabilityPolicy expected_reliability,
  const char* error_message) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<bool>::FromError(validation.Error());
  }

  if (mapping.qos.reliability != expected_reliability) {
    return core::Result<bool>::FromError(MakeError(error_message));
  }

  return core::Result<bool>::FromValue(true);
}

}  // namespace

core::Result<DdsDiscoveredEndpoint> DdsDiscoveryCache::ObserveEndpointDiscovery(
  DdsEndpointRole remote_role,
  const dds::rtps::RtpsMessage& message,
  std::uint64_t now_ms) {
  auto sequence_number = FindSedpSequenceNumber(remote_role, message);
  if (!sequence_number) {
    return core::Result<DdsDiscoveredEndpoint>::FromError(sequence_number.Error());
  }

  auto endpoint = dds::rtps::ExtractSedpEndpointAnnouncement(message, ToSedpKind(remote_role));
  if (!endpoint) {
    return core::Result<DdsDiscoveredEndpoint>::FromError(endpoint.Error());
  }

  DdsDiscoveredEndpoint discovered{
    .role = remote_role,
    .participant_guid_prefix = endpoint.Value().participant_guid_prefix,
    .endpoint_id = endpoint.Value().endpoint_id,
    .topic_name = std::move(endpoint.Value().topic_name),
    .type_name = std::move(endpoint.Value().type_name),
    .unicast_locator = std::move(endpoint.Value().unicast_locator),
    .reliability_kind = endpoint.Value().reliability_kind,
    .durability_kind = endpoint.Value().durability_kind,
    .last_sequence_number = sequence_number.Value(),
    .last_seen_ms = now_ms,
  };

  auto existing = std::find_if(
    endpoints_.begin(),
    endpoints_.end(),
    [&discovered](const DdsDiscoveredEndpoint& candidate) {
      return SameEndpointIdentity(candidate, discovered);
    });
  if (existing == endpoints_.end()) {
    endpoints_.push_back(std::move(discovered));
    return core::Result<DdsDiscoveredEndpoint>::FromValue(endpoints_.back());
  }

  if (discovered.last_sequence_number <= existing->last_sequence_number) {
    return core::Result<DdsDiscoveredEndpoint>::FromError(
      MakeError("DDS endpoint discovery announcement is stale or replayed"));
  }

  *existing = std::move(discovered);
  return core::Result<DdsDiscoveredEndpoint>::FromValue(*existing);
}

core::Result<std::vector<DdsEndpointMatch>> DdsDiscoveryCache::MatchesFor(
  const DdsTopicMapping& local_mapping,
  DdsEndpointRole local_role,
  std::uint64_t now_ms,
  std::uint64_t endpoint_ttl_ms) const {
  auto validation = ValidateMapping(local_mapping);
  if (!validation) {
    return core::Result<std::vector<DdsEndpointMatch>>::FromError(validation.Error());
  }

  if (endpoint_ttl_ms == 0U) {
    return core::Result<std::vector<DdsEndpointMatch>>::FromError(
      MakeError("DDS endpoint discovery TTL is zero"));
  }

  std::vector<DdsEndpointMatch> matches;
  for (const auto& endpoint : endpoints_) {
    if (IsExpired(endpoint, now_ms, endpoint_ttl_ms)) {
      continue;
    }

    auto match = BuildEndpointMatch(local_mapping, local_role, endpoint);
    if (match) {
      matches.push_back(std::move(match.Value()));
    }
  }

  return core::Result<std::vector<DdsEndpointMatch>>::FromValue(std::move(matches));
}

core::Result<std::size_t> DdsDiscoveryCache::RemoveStaleEndpoints(
  std::uint64_t now_ms,
  std::uint64_t endpoint_ttl_ms) {
  if (endpoint_ttl_ms == 0U) {
    return core::Result<std::size_t>::FromError(MakeError("DDS endpoint discovery TTL is zero"));
  }

  const auto original_size = endpoints_.size();
  endpoints_.erase(
    std::remove_if(
      endpoints_.begin(),
      endpoints_.end(),
      [now_ms, endpoint_ttl_ms](const DdsDiscoveredEndpoint& endpoint) {
        return IsExpired(endpoint, now_ms, endpoint_ttl_ms);
      }),
    endpoints_.end());
  return core::Result<std::size_t>::FromValue(original_size - endpoints_.size());
}

DdsBestEffortWriter::DdsBestEffortWriter(DdsTopicMapping mapping)
    : mapping_(std::move(mapping)) {}

core::Result<dds::rtps::RtpsMessage> DdsBestEffortWriter::BuildSample(
  const DdsEndpointMatch& match,
  const com::EventSample& sample) {
  auto profile_validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kBestEffort,
    "DDS best-effort writer requires a best-effort QoS profile");
  if (!profile_validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(profile_validation.Error());
  }

  auto match_validation = ValidateEndpointMatch(mapping_, match, DdsEndpointRole::kPublication);
  if (!match_validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(match_validation.Error());
  }

  auto message = BuildDataMessageForReader(
    mapping_,
    match.remote_endpoint.endpoint_id,
    sample,
    next_sequence_number_);
  if (!message) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(message.Error());
  }

  ++next_sequence_number_;
  ++samples_sent_;
  return message;
}

core::Result<std::size_t> DdsBestEffortWriter::PublishSample(
  const dds::rtps::UdpEndpoint& endpoint,
  const DdsEndpointMatch& match,
  const com::EventSample& sample) {
  auto message = BuildSample(match, sample);
  if (!message) {
    return core::Result<std::size_t>::FromError(message.Error());
  }

  return endpoint.SendTo(message.Value(), match.remote_endpoint.unicast_locator);
}

DdsWriterSnapshot DdsBestEffortWriter::Snapshot() const noexcept {
  return {
    .next_sequence_number = next_sequence_number_,
    .samples_sent = samples_sent_,
  };
}

DdsBestEffortReader::DdsBestEffortReader(DdsTopicMapping mapping)
    : mapping_(std::move(mapping)) {}

core::Result<com::EventSample> DdsBestEffortReader::AcceptSample(
  const DdsEndpointMatch& match,
  const dds::rtps::RtpsMessage& message,
  std::uint64_t now_ms) {
  auto profile_validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kBestEffort,
    "DDS best-effort reader requires a best-effort QoS profile");
  if (!profile_validation) {
    return core::Result<com::EventSample>::FromError(profile_validation.Error());
  }

  auto match_validation = ValidateEndpointMatch(mapping_, match, DdsEndpointRole::kSubscription);
  if (!match_validation) {
    return core::Result<com::EventSample>::FromError(match_validation.Error());
  }

  if (message.guid_prefix != match.remote_endpoint.participant_guid_prefix) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS DATA participant GUID prefix does not match discovered writer"));
  }

  auto data = FindMatchedDataSubmessage(match, message);
  if (!data) {
    return core::Result<com::EventSample>::FromError(data.Error());
  }

  auto writer = FindWriterState(writer_states_, match);
  const bool writer_known = writer != writer_states_.end();
  if (!writer_known && IsLimitEnabled(mapping_.qos.max_remote_writers) &&
      writer_states_.size() >= mapping_.qos.max_remote_writers) {
    ++resource_limit_rejections_;
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS best-effort reader remote-writer resource limit reached"));
  }

  if (writer != writer_states_.end() &&
      data.Value()->writer_sequence_number <= writer->last_sequence_number) {
    ++stale_samples_rejected_;
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS best-effort DATA sample is stale or duplicate"));
  }

  if (writer_known &&
      ExceedsDuration(now_ms, writer->last_seen_ms, mapping_.qos.deadline_ms)) {
    ++deadline_misses_;
  }

  auto decoded = DecodeSamplePayload(mapping_, data.Value()->serialized_payload);
  if (!decoded) {
    return core::Result<com::EventSample>::FromError(decoded.Error());
  }

  if (!writer_known) {
    writer_states_.push_back({
      .participant_guid_prefix = match.remote_endpoint.participant_guid_prefix,
      .writer_id = match.remote_endpoint.endpoint_id,
      .last_sequence_number = data.Value()->writer_sequence_number,
      .last_seen_ms = now_ms,
    });
  } else {
    writer->last_sequence_number = data.Value()->writer_sequence_number;
    writer->last_seen_ms = now_ms;
  }

  ++samples_received_;
  com::EventSample sample = std::move(decoded.Value());
  sample.sequence = data.Value()->writer_sequence_number;
  return core::Result<com::EventSample>::FromValue(std::move(sample));
}

core::Result<com::EventSample> DdsBestEffortReader::ReceiveSample(
  const dds::rtps::UdpEndpoint& endpoint,
  const DdsEndpointMatch& match,
  std::uint64_t now_ms) {
  auto datagram = endpoint.Receive();
  if (!datagram) {
    return core::Result<com::EventSample>::FromError(datagram.Error());
  }

  return AcceptSample(match, datagram.Value().message, now_ms);
}

core::Result<std::size_t> DdsBestEffortReader::RemoveExpiredWriters(std::uint64_t now_ms) {
  auto validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kBestEffort,
    "DDS best-effort reader requires a best-effort QoS profile");
  if (!validation) {
    return core::Result<std::size_t>::FromError(validation.Error());
  }

  const auto removed = RemoveExpiredWriterStates(
    writer_states_,
    now_ms,
    mapping_.qos.liveliness_lease_duration_ms);
  liveliness_lost_ += removed;
  return core::Result<std::size_t>::FromValue(removed);
}

DdsReaderSnapshot DdsBestEffortReader::Snapshot() const {
  return {
    .samples_received = samples_received_,
    .stale_samples_rejected = stale_samples_rejected_,
    .writer_states = writer_states_,
    .deadline_misses = deadline_misses_,
    .liveliness_lost = liveliness_lost_,
    .resource_limit_rejections = resource_limit_rejections_,
  };
}

DdsReliableWriter::DdsReliableWriter(DdsTopicMapping mapping, std::size_t history_depth)
    : mapping_(std::move(mapping)), history_depth_(history_depth) {}

core::Result<dds::rtps::RtpsMessage> DdsReliableWriter::BuildSample(
  const DdsEndpointMatch& match,
  const com::EventSample& sample) {
  auto profile_validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kReliable,
    "DDS reliable writer requires a reliable QoS profile");
  if (!profile_validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(profile_validation.Error());
  }

  auto match_validation = ValidateEndpointMatch(mapping_, match, DdsEndpointRole::kPublication);
  if (!match_validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(match_validation.Error());
  }

  if (history_depth_ == 0U) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS reliable writer history depth is zero"));
  }

  if (history_depth_ < mapping_.qos.history_depth) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS reliable writer history depth is below the QoS profile"));
  }

  if (IsLimitEnabled(mapping_.qos.max_cached_samples) &&
      history_depth_ > mapping_.qos.max_cached_samples) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS reliable writer cached-sample resource limit exceeded"));
  }

  auto message = BuildDataMessageForReader(
    mapping_,
    match.remote_endpoint.endpoint_id,
    sample,
    next_sequence_number_);
  if (!message) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(message.Error());
  }

  history_.push_back({
    .sequence_number = next_sequence_number_,
    .message = message.Value(),
  });
  while (history_.size() > history_depth_) {
    history_.erase(history_.begin());
  }

  ++next_sequence_number_;
  ++samples_sent_;
  return message;
}

core::Result<dds::rtps::RtpsMessage> DdsReliableWriter::BuildHeartbeat(
  const DdsEndpointMatch& match,
  bool final_flag) {
  auto profile_validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kReliable,
    "DDS reliable writer requires a reliable QoS profile");
  if (!profile_validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(profile_validation.Error());
  }

  auto match_validation = ValidateEndpointMatch(mapping_, match, DdsEndpointRole::kPublication);
  if (!match_validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(match_validation.Error());
  }

  if (history_.empty()) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS reliable writer history is empty"));
  }

  ++heartbeat_count_;
  return core::Result<dds::rtps::RtpsMessage>::FromValue({
    .vendor_id = {},
    .guid_prefix = mapping_.participant_guid_prefix,
    .heartbeats = {{
      .reader_id = match.remote_endpoint.endpoint_id,
      .writer_id = mapping_.writer_id,
      .first_sequence_number = history_.front().sequence_number,
      .last_sequence_number = history_.back().sequence_number,
      .count = static_cast<std::uint32_t>(heartbeat_count_),
      .final_flag = final_flag,
    }},
  });
}

core::Result<std::vector<dds::rtps::RtpsMessage>> DdsReliableWriter::BuildRepairSamples(
  const DdsEndpointMatch& match,
  const dds::rtps::RtpsMessage& acknack_message) {
  auto profile_validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kReliable,
    "DDS reliable writer requires a reliable QoS profile");
  if (!profile_validation) {
    return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromError(
      profile_validation.Error());
  }

  auto match_validation = ValidateEndpointMatch(mapping_, match, DdsEndpointRole::kPublication);
  if (!match_validation) {
    return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromError(
      match_validation.Error());
  }

  if (acknack_message.guid_prefix != match.remote_endpoint.participant_guid_prefix) {
    return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromError(
      MakeError("DDS ACKNACK participant GUID prefix does not match discovered reader"));
  }

  auto acknack = FindMatchedAckNack(match, acknack_message);
  if (!acknack) {
    return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromError(acknack.Error());
  }

  std::vector<dds::rtps::RtpsMessage> repairs;
  for (const auto requested_sequence : acknack.Value()->missing_sequence_numbers) {
    auto history = std::find_if(
      history_.begin(),
      history_.end(),
      [requested_sequence](const HistoryEntry& entry) {
        return entry.sequence_number == requested_sequence;
      });
    if (history == history_.end()) {
      return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromError(
        MakeError("DDS repair sample is no longer in writer history"));
    }

    repairs.push_back(history->message);
  }

  repairs_sent_ += repairs.size();
  return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromValue(std::move(repairs));
}

core::Result<std::vector<dds::rtps::RtpsMessage>> DdsReliableWriter::BuildHistoricalSamples(
  const DdsEndpointMatch& match) {
  auto profile_validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kReliable,
    "DDS reliable writer requires a reliable QoS profile");
  if (!profile_validation) {
    return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromError(
      profile_validation.Error());
  }

  if (mapping_.qos.durability != DdsDurabilityPolicy::kTransientLocal) {
    return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromError(
      MakeError("DDS historical replay requires transient-local durability QoS"));
  }

  auto match_validation = ValidateEndpointMatch(mapping_, match, DdsEndpointRole::kPublication);
  if (!match_validation) {
    return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromError(
      match_validation.Error());
  }

  std::vector<dds::rtps::RtpsMessage> historical_samples;
  historical_samples.reserve(history_.size());
  for (const auto& entry : history_) {
    auto message = entry.message;
    for (auto& data : message.data) {
      data.reader_id = match.remote_endpoint.endpoint_id;
    }
    historical_samples.push_back(std::move(message));
  }

  historical_replays_sent_ += historical_samples.size();
  return core::Result<std::vector<dds::rtps::RtpsMessage>>::FromValue(
    std::move(historical_samples));
}

DdsReliableWriterSnapshot DdsReliableWriter::Snapshot() const {
  std::vector<std::uint64_t> history_sequences;
  history_sequences.reserve(history_.size());
  for (const auto& entry : history_) {
    history_sequences.push_back(entry.sequence_number);
  }

  return {
    .next_sequence_number = next_sequence_number_,
    .samples_sent = samples_sent_,
    .heartbeat_count = heartbeat_count_,
    .repairs_sent = repairs_sent_,
    .historical_replays_sent = historical_replays_sent_,
    .history_sequence_numbers = std::move(history_sequences),
  };
}

DdsReliableReader::DdsReliableReader(DdsTopicMapping mapping)
    : mapping_(std::move(mapping)) {}

core::Result<com::EventSample> DdsReliableReader::AcceptSample(
  const DdsEndpointMatch& match,
  const dds::rtps::RtpsMessage& message,
  std::uint64_t now_ms) {
  auto profile_validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kReliable,
    "DDS reliable reader requires a reliable QoS profile");
  if (!profile_validation) {
    return core::Result<com::EventSample>::FromError(profile_validation.Error());
  }

  auto match_validation = ValidateEndpointMatch(mapping_, match, DdsEndpointRole::kSubscription);
  if (!match_validation) {
    return core::Result<com::EventSample>::FromError(match_validation.Error());
  }

  if (message.guid_prefix != match.remote_endpoint.participant_guid_prefix) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS DATA participant GUID prefix does not match discovered writer"));
  }

  auto data = FindMatchedDataSubmessage(match, message);
  if (!data) {
    return core::Result<com::EventSample>::FromError(data.Error());
  }

  auto writer = FindWriterState(writer_states_, match);
  const bool writer_known = writer != writer_states_.end();
  if (!writer_known && IsLimitEnabled(mapping_.qos.max_remote_writers) &&
      writer_states_.size() >= mapping_.qos.max_remote_writers) {
    ++resource_limit_rejections_;
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS reliable reader remote-writer resource limit reached"));
  }

  if (ContainsSequence(received_sequence_numbers_, data.Value()->writer_sequence_number)) {
    ++duplicate_samples_rejected_;
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS reliable DATA sample is duplicate"));
  }

  if (IsLimitEnabled(mapping_.qos.max_cached_samples) &&
      received_sequence_numbers_.size() >= mapping_.qos.max_cached_samples) {
    ++resource_limit_rejections_;
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS reliable reader cached-sample resource limit reached"));
  }

  if (writer_known &&
      ExceedsDuration(now_ms, writer->last_seen_ms, mapping_.qos.deadline_ms)) {
    ++deadline_misses_;
  }

  auto decoded = DecodeSamplePayload(mapping_, data.Value()->serialized_payload);
  if (!decoded) {
    return core::Result<com::EventSample>::FromError(decoded.Error());
  }

  received_sequence_numbers_.push_back(data.Value()->writer_sequence_number);
  std::sort(received_sequence_numbers_.begin(), received_sequence_numbers_.end());
  if (!writer_known) {
    writer_states_.push_back({
      .participant_guid_prefix = match.remote_endpoint.participant_guid_prefix,
      .writer_id = match.remote_endpoint.endpoint_id,
      .last_sequence_number = data.Value()->writer_sequence_number,
      .last_seen_ms = now_ms,
    });
  } else {
    writer->last_sequence_number =
      std::max(writer->last_sequence_number, data.Value()->writer_sequence_number);
    writer->last_seen_ms = now_ms;
  }
  ++samples_received_;

  com::EventSample sample = std::move(decoded.Value());
  sample.sequence = data.Value()->writer_sequence_number;
  return core::Result<com::EventSample>::FromValue(std::move(sample));
}

core::Result<dds::rtps::RtpsMessage> DdsReliableReader::BuildAckNack(
  const DdsEndpointMatch& match,
  const dds::rtps::RtpsMessage& heartbeat_message) {
  auto profile_validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kReliable,
    "DDS reliable reader requires a reliable QoS profile");
  if (!profile_validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(profile_validation.Error());
  }

  auto match_validation = ValidateEndpointMatch(mapping_, match, DdsEndpointRole::kSubscription);
  if (!match_validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(match_validation.Error());
  }

  if (heartbeat_message.guid_prefix != match.remote_endpoint.participant_guid_prefix) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS HEARTBEAT participant GUID prefix does not match discovered writer"));
  }

  auto heartbeat = FindMatchedHeartbeat(match, heartbeat_message);
  if (!heartbeat) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(heartbeat.Error());
  }

  if (heartbeat.Value()->first_sequence_number == 0U ||
      heartbeat.Value()->last_sequence_number < heartbeat.Value()->first_sequence_number) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS HEARTBEAT sequence range is invalid"));
  }

  const auto heartbeat_range =
    heartbeat.Value()->last_sequence_number - heartbeat.Value()->first_sequence_number + 1U;
  if (heartbeat_range > dds::rtps::kMaxAckNackBitmapBits) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS HEARTBEAT range exceeds ACKNACK bitmap limit"));
  }

  std::vector<std::uint64_t> missing;
  for (std::uint64_t sequence = heartbeat.Value()->first_sequence_number;
       sequence <= heartbeat.Value()->last_sequence_number;
       ++sequence) {
    if (!ContainsSequence(received_sequence_numbers_, sequence)) {
      missing.push_back(sequence);
    }
  }

  ++acknack_count_;
  const bool no_missing_samples = missing.empty();
  return core::Result<dds::rtps::RtpsMessage>::FromValue({
    .vendor_id = {},
    .guid_prefix = mapping_.participant_guid_prefix,
    .acknacks = {{
      .reader_id = mapping_.reader_id,
      .writer_id = match.remote_endpoint.endpoint_id,
      .bitmap_base = heartbeat.Value()->first_sequence_number,
      .missing_sequence_numbers = std::move(missing),
      .count = static_cast<std::uint32_t>(acknack_count_),
      .final_flag = no_missing_samples,
    }},
  });
}

core::Result<std::size_t> DdsReliableReader::RemoveExpiredWriters(std::uint64_t now_ms) {
  auto validation = ValidateTransportClassProfile(
    mapping_,
    DdsReliabilityPolicy::kReliable,
    "DDS reliable reader requires a reliable QoS profile");
  if (!validation) {
    return core::Result<std::size_t>::FromError(validation.Error());
  }

  const auto removed = RemoveExpiredWriterStates(
    writer_states_,
    now_ms,
    mapping_.qos.liveliness_lease_duration_ms);
  liveliness_lost_ += removed;
  return core::Result<std::size_t>::FromValue(removed);
}

DdsReliableReaderSnapshot DdsReliableReader::Snapshot() const {
  return {
    .samples_received = samples_received_,
    .duplicate_samples_rejected = duplicate_samples_rejected_,
    .acknack_count = acknack_count_,
    .received_sequence_numbers = received_sequence_numbers_,
    .writer_states = writer_states_,
    .deadline_misses = deadline_misses_,
    .liveliness_lost = liveliness_lost_,
    .resource_limit_rejections = resource_limit_rejections_,
  };
}

core::Result<dds::rtps::RtpsMessage> DdsBinding::BuildDataMessage(
  const DdsTopicMapping& mapping,
  const com::EventSample& sample,
  std::uint64_t sequence_number) {
  return BuildDataMessageForReader(mapping, mapping.reader_id, sample, sequence_number);
}

core::Result<com::EventSample> DdsBinding::DecodeDataMessage(
  const DdsTopicMapping& mapping,
  const dds::rtps::RtpsMessage& message) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<com::EventSample>::FromError(validation.Error());
  }

  if (message.guid_prefix != mapping.participant_guid_prefix) {
    return core::Result<com::EventSample>::FromError(
      MakeError("DDS participant GUID prefix does not match mapping"));
  }

  for (const auto& data : message.data) {
    if (data.writer_id == mapping.writer_id && data.reader_id == mapping.reader_id) {
      return DecodeSamplePayload(mapping, data.serialized_payload);
    }
  }

  return core::Result<com::EventSample>::FromError(
    MakeError("DDS RTPS message does not contain the mapped DATA writer/reader pair"));
}

core::Result<dds::rtps::RtpsMessage> DdsBinding::BuildMethodRequest(
  const DdsMethodMapping& mapping,
  const com::MethodCall& call,
  std::uint64_t sequence_number) {
  auto validation = ValidateMethodRequestMapping(mapping);
  if (!validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(validation.Error());
  }

  if (call.service != mapping.ara_service) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("method call service does not match DDS mapping"));
  }

  if (call.method_name != mapping.method_name) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("method call name does not match DDS mapping"));
  }

  auto serialized_payload = EncodeMethodPayload(
    mapping,
    call.payload,
    call.correlation_id,
    false,
    call.expects_response,
    false);
  if (!serialized_payload) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(serialized_payload.Error());
  }

  return BuildMethodDataMessage(
    mapping,
    mapping.request_writer_id,
    mapping.request_reader_id,
    std::move(serialized_payload.Value()),
    sequence_number,
    "DDS RTPS method request exceeds configured UDP payload limit");
}

core::Result<com::MethodCall> DdsBinding::DecodeMethodRequest(
  const DdsMethodMapping& mapping,
  const dds::rtps::RtpsMessage& message) {
  auto validation = ValidateMethodRequestMapping(mapping);
  if (!validation) {
    return core::Result<com::MethodCall>::FromError(validation.Error());
  }

  if (message.guid_prefix != mapping.participant_guid_prefix) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("DDS method request participant GUID prefix does not match mapping"));
  }

  auto data = FindMethodDataSubmessage(
    message,
    mapping.request_writer_id,
    mapping.request_reader_id);
  if (!data) {
    return core::Result<com::MethodCall>::FromError(data.Error());
  }

  auto decoded = DecodeMethodPayload(mapping, data.Value()->serialized_payload, false);
  if (!decoded) {
    return core::Result<com::MethodCall>::FromError(decoded.Error());
  }
  auto decoded_value = std::move(decoded.Value());

  return core::Result<com::MethodCall>::FromValue({
    .service = mapping.ara_service,
    .method_name = mapping.method_name,
    .payload = std::move(decoded_value.payload),
    .correlation_id = decoded_value.correlation_id,
    .expects_response = decoded_value.expects_response,
  });
}

core::Result<dds::rtps::RtpsMessage> DdsBinding::BuildMethodResponse(
  const DdsMethodMapping& mapping,
  const com::MethodResult& result,
  std::uint64_t sequence_number) {
  auto validation = ValidateMethodResponseMapping(mapping);
  if (!validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(validation.Error());
  }

  if (result.service != mapping.ara_service) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("method result service does not match DDS mapping"));
  }

  if (result.method_name != mapping.method_name) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("method result name does not match DDS mapping"));
  }

  auto serialized_payload = EncodeMethodPayload(
    mapping,
    result.payload,
    result.correlation_id,
    true,
    true,
    result.application_error,
    result.error_domain,
    result.error_code);
  if (!serialized_payload) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(serialized_payload.Error());
  }

  return BuildMethodDataMessage(
    mapping,
    mapping.response_writer_id,
    mapping.response_reader_id,
    std::move(serialized_payload.Value()),
    sequence_number,
    "DDS RTPS method response exceeds configured UDP payload limit");
}

core::Result<com::MethodResult> DdsBinding::DecodeMethodResponse(
  const DdsMethodMapping& mapping,
  const dds::rtps::RtpsMessage& message) {
  auto validation = ValidateMethodResponseMapping(mapping);
  if (!validation) {
    return core::Result<com::MethodResult>::FromError(validation.Error());
  }

  if (message.guid_prefix != mapping.participant_guid_prefix) {
    return core::Result<com::MethodResult>::FromError(
      MakeError("DDS method response participant GUID prefix does not match mapping"));
  }

  auto data = FindMethodDataSubmessage(
    message,
    mapping.response_writer_id,
    mapping.response_reader_id);
  if (!data) {
    return core::Result<com::MethodResult>::FromError(data.Error());
  }

  auto decoded = DecodeMethodPayload(mapping, data.Value()->serialized_payload, true);
  if (!decoded) {
    return core::Result<com::MethodResult>::FromError(decoded.Error());
  }
  auto decoded_value = std::move(decoded.Value());

  return core::Result<com::MethodResult>::FromValue({
    .service = mapping.ara_service,
    .method_name = mapping.method_name,
    .payload = std::move(decoded_value.payload),
    .correlation_id = decoded_value.correlation_id,
    .application_error = decoded_value.application_error,
    .error_domain = std::move(decoded_value.error_domain),
    .error_code = decoded_value.error_code,
  });
}

core::Result<dds::rtps::RtpsMessage> DdsBinding::BuildFieldNotification(
  const DdsFieldMapping& mapping,
  const com::FieldValue& value,
  std::uint64_t sequence_number) {
  auto validation = ValidateFieldMapping(mapping);
  if (!validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(validation.Error());
  }

  if (sequence_number == 0U) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS field DATA sequence number is zero"));
  }

  auto serialized_payload = EncodeFieldPayload(mapping, value);
  if (!serialized_payload) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(serialized_payload.Error());
  }

  dds::rtps::RtpsMessage message{
    .vendor_id = {},
    .guid_prefix = mapping.participant_guid_prefix,
    .data = {{
      .reader_id = mapping.reader_id,
      .writer_id = mapping.writer_id,
      .writer_sequence_number = sequence_number,
      .serialized_payload = std::move(serialized_payload.Value()),
    }},
  };

  if (!dds::rtps::FitsUdpPayload(message)) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(
      MakeError("DDS RTPS field notification exceeds configured UDP payload limit"));
  }

  return core::Result<dds::rtps::RtpsMessage>::FromValue(std::move(message));
}

core::Result<com::FieldValue> DdsBinding::DecodeFieldNotification(
  const DdsFieldMapping& mapping,
  const dds::rtps::RtpsMessage& message) {
  auto validation = ValidateFieldMapping(mapping);
  if (!validation) {
    return core::Result<com::FieldValue>::FromError(validation.Error());
  }

  if (message.guid_prefix != mapping.participant_guid_prefix) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("DDS field participant GUID prefix does not match mapping"));
  }

  for (const auto& data : message.data) {
    if (data.writer_id != mapping.writer_id || data.reader_id != mapping.reader_id) {
      continue;
    }

    if (data.writer_sequence_number == 0U) {
      return core::Result<com::FieldValue>::FromError(
        MakeError("DDS field DATA sequence number is zero"));
    }

    auto decoded = DecodeFieldPayload(mapping, data.serialized_payload);
    if (!decoded) {
      return core::Result<com::FieldValue>::FromError(decoded.Error());
    }

    if (decoded.Value().sequence == 0U) {
      decoded.Value().sequence = data.writer_sequence_number;
    }
    return decoded;
  }

  return core::Result<com::FieldValue>::FromError(
    MakeError("DDS RTPS message does not contain the mapped field DATA pair"));
}

core::Result<std::size_t> DdsBinding::PublishEvent(
  const dds::rtps::UdpEndpoint& endpoint,
  dds::rtps::UdpEndpointAddress remote,
  const DdsTopicMapping& mapping,
  const com::EventSample& sample,
  std::uint64_t sequence_number) {
  auto message = BuildDataMessage(mapping, sample, sequence_number);
  if (!message) {
    return core::Result<std::size_t>::FromError(message.Error());
  }

  return endpoint.SendTo(message.Value(), std::move(remote));
}

core::Result<com::EventSample> DdsBinding::ReceiveEvent(
  const dds::rtps::UdpEndpoint& endpoint,
  const DdsTopicMapping& mapping) {
  auto datagram = endpoint.Receive();
  if (!datagram) {
    return core::Result<com::EventSample>::FromError(datagram.Error());
  }

  return DecodeDataMessage(mapping, datagram.Value().message);
}

core::Result<std::size_t> DdsBinding::PublishFieldNotification(
  const dds::rtps::UdpEndpoint& endpoint,
  dds::rtps::UdpEndpointAddress remote,
  const DdsFieldMapping& mapping,
  const com::FieldValue& value,
  std::uint64_t sequence_number) {
  auto message = BuildFieldNotification(mapping, value, sequence_number);
  if (!message) {
    return core::Result<std::size_t>::FromError(message.Error());
  }

  return endpoint.SendTo(message.Value(), std::move(remote));
}

core::Result<com::FieldValue> DdsBinding::ReceiveFieldNotification(
  const dds::rtps::UdpEndpoint& endpoint,
  const DdsFieldMapping& mapping) {
  auto datagram = endpoint.Receive();
  if (!datagram) {
    return core::Result<com::FieldValue>::FromError(datagram.Error());
  }

  return DecodeFieldNotification(mapping, datagram.Value().message);
}

core::Result<dds::rtps::RtpsMessage> DdsBinding::BuildEndpointDiscovery(
  const DdsTopicMapping& mapping,
  DdsEndpointRole role,
  dds::rtps::UdpEndpointAddress locator,
  std::uint64_t sequence_number) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(validation.Error());
  }

  return dds::rtps::BuildSedpEndpointAnnouncement(
    {
      .participant_guid_prefix = mapping.participant_guid_prefix,
      .endpoint_id = EndpointIdForRole(mapping, role),
      .topic_name = mapping.topic_name,
      .type_name = mapping.type_name,
      .unicast_locator = std::move(locator),
      .reliability_kind = ToSedpReliability(mapping.qos.reliability),
      .durability_kind = ToSedpDurability(mapping.qos.durability),
    },
    ToSedpKind(role),
    sequence_number);
}

core::Result<dds::rtps::RtpsMessage> DdsBinding::BuildEndpointDiscovery(
  const DdsFieldMapping& mapping,
  DdsEndpointRole role,
  dds::rtps::UdpEndpointAddress locator,
  std::uint64_t sequence_number) {
  auto validation = ValidateFieldMapping(mapping);
  if (!validation) {
    return core::Result<dds::rtps::RtpsMessage>::FromError(validation.Error());
  }

  return dds::rtps::BuildSedpEndpointAnnouncement(
    {
      .participant_guid_prefix = mapping.participant_guid_prefix,
      .endpoint_id = EndpointIdForRole(mapping, role),
      .topic_name = mapping.topic_name,
      .type_name = mapping.type_name,
      .unicast_locator = std::move(locator),
      .reliability_kind = ToSedpReliability(mapping.qos.reliability),
      .durability_kind = ToSedpDurability(mapping.qos.durability),
    },
    ToSedpKind(role),
    sequence_number);
}

std::vector<UnsupportedFeature> DdsBinding::UnsupportedFeatureMatrix() {
  return {
    {
      "DDS discovery lifecycle",
      "SPDP/SEDP codecs and deterministic endpoint matching are present; "
      "multicast lifecycle callbacks are not implemented",
    },
    {
      "QoS negotiation",
      "static reliability/durability/history/deadline/liveliness/resource profiles are present; "
      "dynamic runtime negotiation is not",
    },
    {
      "reliable writer/reader",
      "bounded HEARTBEAT/ACKNACK repair path is present; timers and async resend are not",
    },
    {
      "async method dispatch",
      "DDS request/reply, no-response method, and field notification envelopes "
      "are present; proxy/skeleton dispatch loops are not implemented",
    },
    {"fragmentation", "UDP payloads are bounded to one RTPS datagram in the MVP"},
    {
      "durability/history cache",
      "transient-local replay from bounded writer history is present; "
      "persistence across restart is not",
    },
  };
}

}  // namespace openautosar::dds_binding
