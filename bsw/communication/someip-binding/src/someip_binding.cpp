// SPDX-License-Identifier: MIT

#include "openautosar/someip_binding/someip_binding.h"

#include "openautosar/someip/service_discovery.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace openautosar::someip_binding {
namespace {

inline constexpr std::uint32_t kMaxSdTtlSeconds{0x00FFFFFFU};
inline constexpr std::uint16_t kEventIdFlag{0x8000U};
inline constexpr std::array<std::uint8_t, 4U> kMethodErrorPayloadMagic{'O', 'A', 'E', 'R'};
inline constexpr std::uint8_t kMethodErrorPayloadVersion{1U};
inline constexpr std::size_t kMethodErrorPayloadHeaderSize{16U};

struct MethodErrorPayload final {
  std::vector<std::uint8_t> payload;
  std::string error_domain;
  std::uint32_t error_code{0U};
  bool structured{false};
};

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"someip-binding", message};
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
  const std::vector<std::uint8_t>& bytes,
  std::size_t offset) {
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(bytes[offset]) |
    static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32Le(
  const std::vector<std::uint8_t>& bytes,
  std::size_t offset) {
  return static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(bytes[offset]) |
    static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U |
    static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U |
    static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] bool HasMethodErrorPayloadMagic(
  const std::vector<std::uint8_t>& payload) {
  return payload.size() >= kMethodErrorPayloadMagic.size() &&
         std::equal(
           kMethodErrorPayloadMagic.begin(),
           kMethodErrorPayloadMagic.end(),
           payload.begin());
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>> EncodeMethodErrorPayload(
  const com::MethodResult& result) {
  if (!result.application_error ||
      (result.error_domain.empty() && result.error_code == 0U)) {
    return core::Result<std::vector<std::uint8_t>>::FromValue(result.payload);
  }

  if (result.error_domain.empty()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("method error domain is empty"));
  }

  if (result.error_domain.size() > std::numeric_limits<std::uint16_t>::max()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("method error domain is too long"));
  }

  if (result.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("method error payload length exceeds 32-bit field"));
  }

  std::vector<std::uint8_t> payload;
  payload.reserve(
    kMethodErrorPayloadHeaderSize + result.error_domain.size() + result.payload.size());
  payload.insert(
    payload.end(),
    kMethodErrorPayloadMagic.begin(),
    kMethodErrorPayloadMagic.end());
  payload.push_back(kMethodErrorPayloadVersion);
  payload.push_back(0U);
  WriteU16Le(payload, static_cast<std::uint16_t>(result.error_domain.size()));
  WriteU32Le(payload, result.error_code);
  WriteU32Le(payload, static_cast<std::uint32_t>(result.payload.size()));
  payload.insert(payload.end(), result.error_domain.begin(), result.error_domain.end());
  payload.insert(payload.end(), result.payload.begin(), result.payload.end());
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(payload));
}

[[nodiscard]] core::Result<MethodErrorPayload> DecodeMethodErrorPayload(
  const std::vector<std::uint8_t>& payload) {
  if (!HasMethodErrorPayloadMagic(payload)) {
    return core::Result<MethodErrorPayload>::FromValue({
      .payload = payload,
      .error_domain = {},
      .error_code = 0U,
      .structured = false,
    });
  }

  if (payload.size() < kMethodErrorPayloadHeaderSize) {
    return core::Result<MethodErrorPayload>::FromError(
      MakeError("method error payload is shorter than envelope header"));
  }

  if (payload[4U] != kMethodErrorPayloadVersion || payload[5U] != 0U) {
    return core::Result<MethodErrorPayload>::FromError(
      MakeError("method error payload version is unsupported"));
  }

  const auto domain_length = ReadU16Le(payload, 6U);
  const auto error_code = ReadU32Le(payload, 8U);
  const auto value_length = ReadU32Le(payload, 12U);
  const auto expected_size = kMethodErrorPayloadHeaderSize + domain_length +
                             static_cast<std::size_t>(value_length);
  if (payload.size() != expected_size) {
    return core::Result<MethodErrorPayload>::FromError(
      MakeError("method error payload length mismatch"));
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

[[nodiscard]] bool MatchesService(
  const com::ServiceIdentifier& left,
  const com::ServiceIdentifier& right) noexcept {
  return left == right;
}

[[nodiscard]] core::Result<bool> ValidateServiceMapping(const SomeIpServiceMapping& mapping) {
  if (mapping.ara_service.interface_id == 0U || mapping.ara_service.instance_id == 0U) {
    return core::Result<bool>::FromError(MakeError("ARA service identifier is invalid"));
  }

  if (mapping.ara_service.major_version == 0U || mapping.major_version == 0U) {
    return core::Result<bool>::FromError(MakeError("service major version is invalid"));
  }

  if (mapping.service_id == 0U || mapping.instance_id == 0U) {
    return core::Result<bool>::FromError(MakeError("SOME/IP service or instance id is invalid"));
  }

  if (mapping.ttl_seconds > kMaxSdTtlSeconds) {
    return core::Result<bool>::FromError(MakeError("SOME/IP-SD TTL exceeds 24-bit field"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateEventMapping(const SomeIpServiceMapping& mapping) {
  auto validation = ValidateServiceMapping(mapping);
  if (!validation) {
    return core::Result<bool>::FromError(validation.Error());
  }

  if (mapping.event_id == 0U || mapping.eventgroup_id == 0U) {
    return core::Result<bool>::FromError(MakeError("SOME/IP event or eventgroup id is invalid"));
  }

  if ((mapping.event_id & kEventIdFlag) == 0U) {
    return core::Result<bool>::FromError(MakeError("SOME/IP event id is outside event range"));
  }

  if (mapping.event_name.empty()) {
    return core::Result<bool>::FromError(MakeError("event name is empty"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateMethodMapping(const SomeIpServiceMapping& mapping) {
  auto validation = ValidateServiceMapping(mapping);
  if (!validation) {
    return core::Result<bool>::FromError(validation.Error());
  }

  if (mapping.method_id == 0U) {
    return core::Result<bool>::FromError(MakeError("SOME/IP method id is invalid"));
  }

  if ((mapping.method_id & kEventIdFlag) != 0U) {
    return core::Result<bool>::FromError(MakeError("SOME/IP method id is inside event range"));
  }

  if (mapping.method_name.empty()) {
    return core::Result<bool>::FromError(MakeError("method name is empty"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateFieldCommon(const SomeIpServiceMapping& mapping) {
  auto validation = ValidateServiceMapping(mapping);
  if (!validation) {
    return core::Result<bool>::FromError(validation.Error());
  }

  if (mapping.field_name.empty()) {
    return core::Result<bool>::FromError(MakeError("field name is empty"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateFieldGetterMapping(
  const SomeIpServiceMapping& mapping) {
  auto validation = ValidateFieldCommon(mapping);
  if (!validation) {
    return validation;
  }

  if (mapping.field_getter_id == 0U || (mapping.field_getter_id & kEventIdFlag) != 0U) {
    return core::Result<bool>::FromError(
      MakeError("SOME/IP field getter id is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateFieldSetterMapping(
  const SomeIpServiceMapping& mapping) {
  auto validation = ValidateFieldCommon(mapping);
  if (!validation) {
    return validation;
  }

  if (mapping.field_setter_id == 0U || (mapping.field_setter_id & kEventIdFlag) != 0U) {
    return core::Result<bool>::FromError(
      MakeError("SOME/IP field setter id is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateFieldNotifierMapping(
  const SomeIpServiceMapping& mapping) {
  auto validation = ValidateFieldCommon(mapping);
  if (!validation) {
    return validation;
  }

  if (mapping.field_notifier_id == 0U || (mapping.field_notifier_id & kEventIdFlag) == 0U) {
    return core::Result<bool>::FromError(
      MakeError("SOME/IP field notifier id is outside event range"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateFieldValue(
  const SomeIpServiceMapping& mapping,
  const com::FieldValue& value) {
  if (!MatchesService(value.service, mapping.ara_service)) {
    return core::Result<bool>::FromError(
      MakeError("field value service does not match SOME/IP mapping"));
  }

  if (value.field_name != mapping.field_name) {
    return core::Result<bool>::FromError(
      MakeError("field value name does not match SOME/IP mapping"));
  }

  if (value.payload.empty()) {
    return core::Result<bool>::FromError(MakeError("field value payload is empty"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<someip::Message> BuildFieldMessage(
  const SomeIpServiceMapping& mapping,
  std::uint16_t field_id,
  someip::MessageType message_type,
  std::vector<std::uint8_t> payload,
  someip::RequestId request_id,
  const char* overflow_error_message) {
  someip::Message message{
    .header = {
      .message_id = {.service_id = mapping.service_id, .method_id = field_id},
      .request_id = request_id,
      .protocol_version = someip::kProtocolVersion,
      .interface_version = mapping.major_version,
      .message_type = message_type,
      .return_code = someip::ReturnCode::kOk,
    },
    .payload = std::move(payload),
  };

  if (!someip::FitsUdpPayload(message)) {
    return core::Result<someip::Message>::FromError(MakeError(overflow_error_message));
  }

  return core::Result<someip::Message>::FromValue(std::move(message));
}

[[nodiscard]] core::Result<bool> ValidateFieldMessageHeader(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message,
  std::uint16_t field_id,
  someip::MessageType message_type) {
  if (message.header.message_id.service_id != mapping.service_id ||
      message.header.message_id.method_id != field_id) {
    return core::Result<bool>::FromError(
      MakeError("SOME/IP message id does not match field mapping"));
  }

  if (message.header.protocol_version != someip::kProtocolVersion) {
    return core::Result<bool>::FromError(
      MakeError("SOME/IP protocol version is unsupported"));
  }

  if (message.header.interface_version != mapping.major_version) {
    return core::Result<bool>::FromError(
      MakeError("SOME/IP interface version does not match field mapping"));
  }

  if (message.header.message_type != message_type) {
    return core::Result<bool>::FromError(
      MakeError("SOME/IP field message type does not match mapping"));
  }

  if (message.header.return_code != someip::ReturnCode::kOk) {
    return core::Result<bool>::FromError(
      MakeError("SOME/IP field return code is not OK"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<com::FieldValue> DecodeFieldValue(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message,
  std::uint16_t field_id,
  someip::MessageType message_type) {
  auto validation = ValidateFieldMessageHeader(mapping, message, field_id, message_type);
  if (!validation) {
    return core::Result<com::FieldValue>::FromError(validation.Error());
  }

  if (message.payload.empty()) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("SOME/IP field payload is empty"));
  }

  return core::Result<com::FieldValue>::FromValue({
    .service = mapping.ara_service,
    .field_name = mapping.field_name,
    .payload = message.payload,
    .sequence = 0U,
  });
}

[[nodiscard]] std::uint64_t CorrelationFromRequestId(someip::RequestId request_id) noexcept {
  return (static_cast<std::uint64_t>(request_id.client_id) << 16U) |
         static_cast<std::uint64_t>(request_id.session_id);
}

[[nodiscard]] std::uint32_t OfferTtlSeconds(
  const com::ServiceOffer& offer,
  const SomeIpServiceMapping& mapping) noexcept {
  if (offer.ttl_ms == 0U) {
    return mapping.ttl_seconds;
  }

  const auto seconds = (offer.ttl_ms + 999U) / 1'000U;
  return std::min(seconds, kMaxSdTtlSeconds);
}

[[nodiscard]] someip::sd::ServiceEntry MakeServiceEntry(
  someip::sd::EntryType type,
  const SomeIpServiceMapping& mapping,
  std::uint32_t ttl_seconds,
  std::uint16_t eventgroup_id) {
  return {
    .type = type,
    .service_id = mapping.service_id,
    .instance_id = mapping.instance_id,
    .major_version = mapping.major_version,
    .ttl = ttl_seconds,
    .minor_version = mapping.minor_version,
    .eventgroup_id = eventgroup_id,
  };
}

[[nodiscard]] core::Result<someip::Message> BuildDiscoveryMessage(
  someip::sd::EntryType type,
  const SomeIpServiceMapping& mapping,
  std::uint32_t ttl_seconds,
  std::uint16_t eventgroup_id,
  someip::RequestId request_id) {
  auto validation = ValidateServiceMapping(mapping);
  if (!validation) {
    return core::Result<someip::Message>::FromError(validation.Error());
  }

  return someip::sd::BuildServiceDiscoveryMessage(
    {
      .reboot_session = 0U,
      .entries = {MakeServiceEntry(type, mapping, ttl_seconds, eventgroup_id)},
    },
    request_id);
}

}  // namespace

core::Result<someip::Message> SomeIpBinding::BuildOffer(
  const com::ServiceOffer& offer,
  const SomeIpServiceMapping& mapping,
  someip::RequestId request_id) {
  if (!MatchesService(offer.service, mapping.ara_service)) {
    return core::Result<someip::Message>::FromError(
      MakeError("service offer does not match SOME/IP mapping"));
  }

  if (offer.offer_state != com::OfferState::kOffered) {
    return core::Result<someip::Message>::FromError(MakeError("service offer is not active"));
  }

  return BuildDiscoveryMessage(
    someip::sd::EntryType::kOfferService,
    mapping,
    OfferTtlSeconds(offer, mapping),
    0U,
    request_id);
}

core::Result<someip::Message> SomeIpBinding::BuildFind(
  const SomeIpServiceMapping& mapping,
  someip::RequestId request_id) {
  return BuildDiscoveryMessage(
    someip::sd::EntryType::kFindService,
    mapping,
    0U,
    0U,
    request_id);
}

core::Result<someip::Message> SomeIpBinding::BuildSubscribe(
  const SomeIpServiceMapping& mapping,
  someip::RequestId request_id) {
  auto validation = ValidateEventMapping(mapping);
  if (!validation) {
    return core::Result<someip::Message>::FromError(validation.Error());
  }

  return BuildDiscoveryMessage(
    someip::sd::EntryType::kSubscribeEventgroup,
    mapping,
    mapping.ttl_seconds,
    mapping.eventgroup_id,
    request_id);
}

core::Result<someip::Message> SomeIpBinding::BuildSubscribeAck(
  const SomeIpServiceMapping& mapping,
  someip::RequestId request_id) {
  auto validation = ValidateEventMapping(mapping);
  if (!validation) {
    return core::Result<someip::Message>::FromError(validation.Error());
  }

  return BuildDiscoveryMessage(
    someip::sd::EntryType::kSubscribeEventgroupAck,
    mapping,
    mapping.ttl_seconds,
    mapping.eventgroup_id,
    request_id);
}

core::Result<someip::Message> SomeIpBinding::BuildEventNotification(
  const SomeIpServiceMapping& mapping,
  const com::EventSample& sample,
  someip::RequestId request_id) {
  auto validation = ValidateEventMapping(mapping);
  if (!validation) {
    return core::Result<someip::Message>::FromError(validation.Error());
  }

  if (!MatchesService(sample.service, mapping.ara_service)) {
    return core::Result<someip::Message>::FromError(
      MakeError("event sample service does not match SOME/IP mapping"));
  }

  if (sample.event_name != mapping.event_name) {
    return core::Result<someip::Message>::FromError(
      MakeError("event sample name does not match SOME/IP mapping"));
  }

  if (sample.payload.empty()) {
    return core::Result<someip::Message>::FromError(MakeError("event sample payload is empty"));
  }

  someip::Message message{
    .header = {
      .message_id = {.service_id = mapping.service_id, .method_id = mapping.event_id},
      .request_id = request_id,
      .protocol_version = someip::kProtocolVersion,
      .interface_version = mapping.major_version,
      .message_type = someip::MessageType::kNotification,
      .return_code = someip::ReturnCode::kOk,
    },
    .payload = sample.payload,
  };

  if (!someip::FitsUdpPayload(message)) {
    return core::Result<someip::Message>::FromError(
      MakeError("event notification exceeds configured UDP payload limit"));
  }

  return core::Result<someip::Message>::FromValue(std::move(message));
}

core::Result<com::EventSample> SomeIpBinding::DecodeEventNotification(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message) {
  auto validation = ValidateEventMapping(mapping);
  if (!validation) {
    return core::Result<com::EventSample>::FromError(validation.Error());
  }

  if (message.header.message_id.service_id != mapping.service_id ||
      message.header.message_id.method_id != mapping.event_id) {
    return core::Result<com::EventSample>::FromError(
      MakeError("SOME/IP message id does not match event mapping"));
  }

  if (message.header.protocol_version != someip::kProtocolVersion) {
    return core::Result<com::EventSample>::FromError(
      MakeError("SOME/IP protocol version is unsupported"));
  }

  if (message.header.interface_version != mapping.major_version) {
    return core::Result<com::EventSample>::FromError(
      MakeError("SOME/IP interface version does not match event mapping"));
  }

  if (message.header.message_type != someip::MessageType::kNotification) {
    return core::Result<com::EventSample>::FromError(
      MakeError("SOME/IP event is not a notification"));
  }

  if (message.header.return_code != someip::ReturnCode::kOk) {
    return core::Result<com::EventSample>::FromError(
      MakeError("SOME/IP event return code is not OK"));
  }

  if (message.payload.empty()) {
    return core::Result<com::EventSample>::FromError(MakeError("SOME/IP event payload is empty"));
  }

  return core::Result<com::EventSample>::FromValue({
    .service = mapping.ara_service,
    .event_name = mapping.event_name,
    .payload = message.payload,
    .sequence = 0U,
  });
}

core::Result<someip::Message> SomeIpBinding::BuildMethodRequest(
  const SomeIpServiceMapping& mapping,
  const com::MethodCall& call,
  someip::RequestId request_id) {
  auto validation = ValidateMethodMapping(mapping);
  if (!validation) {
    return core::Result<someip::Message>::FromError(validation.Error());
  }

  if (!MatchesService(call.service, mapping.ara_service)) {
    return core::Result<someip::Message>::FromError(
      MakeError("method call service does not match SOME/IP mapping"));
  }

  if (call.method_name != mapping.method_name) {
    return core::Result<someip::Message>::FromError(
      MakeError("method call name does not match SOME/IP mapping"));
  }

  if (!call.expects_response) {
    return core::Result<someip::Message>::FromError(
      MakeError("method call does not expect a SOME/IP response"));
  }

  const auto request_correlation = CorrelationFromRequestId(request_id);
  if (call.correlation_id != 0U && call.correlation_id != request_correlation) {
    return core::Result<someip::Message>::FromError(
      MakeError("method call correlation does not match SOME/IP request id"));
  }

  someip::Message message{
    .header = {
      .message_id = {.service_id = mapping.service_id, .method_id = mapping.method_id},
      .request_id = request_id,
      .protocol_version = someip::kProtocolVersion,
      .interface_version = mapping.major_version,
      .message_type = someip::MessageType::kRequest,
      .return_code = someip::ReturnCode::kOk,
    },
    .payload = call.payload,
  };

  if (!someip::FitsUdpPayload(message)) {
    return core::Result<someip::Message>::FromError(
      MakeError("method request exceeds configured UDP payload limit"));
  }

  return core::Result<someip::Message>::FromValue(std::move(message));
}

core::Result<com::MethodCall> SomeIpBinding::DecodeMethodRequest(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message) {
  auto validation = ValidateMethodMapping(mapping);
  if (!validation) {
    return core::Result<com::MethodCall>::FromError(validation.Error());
  }

  if (message.header.message_id.service_id != mapping.service_id ||
      message.header.message_id.method_id != mapping.method_id) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP message id does not match method mapping"));
  }

  if (message.header.protocol_version != someip::kProtocolVersion) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP protocol version is unsupported"));
  }

  if (message.header.interface_version != mapping.major_version) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP interface version does not match method mapping"));
  }

  if (message.header.message_type != someip::MessageType::kRequest) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP method message is not a request"));
  }

  if (message.header.return_code != someip::ReturnCode::kOk) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP method request return code is not OK"));
  }

  return core::Result<com::MethodCall>::FromValue({
    .service = mapping.ara_service,
    .method_name = mapping.method_name,
    .payload = message.payload,
    .correlation_id = CorrelationFromRequestId(message.header.request_id),
    .expects_response = true,
  });
}

core::Result<someip::Message> SomeIpBinding::BuildFireAndForgetMethodRequest(
  const SomeIpServiceMapping& mapping,
  const com::MethodCall& call,
  someip::RequestId request_id) {
  auto validation = ValidateMethodMapping(mapping);
  if (!validation) {
    return core::Result<someip::Message>::FromError(validation.Error());
  }

  if (!MatchesService(call.service, mapping.ara_service)) {
    return core::Result<someip::Message>::FromError(
      MakeError("method call service does not match SOME/IP mapping"));
  }

  if (call.method_name != mapping.method_name) {
    return core::Result<someip::Message>::FromError(
      MakeError("method call name does not match SOME/IP mapping"));
  }

  if (call.expects_response) {
    return core::Result<someip::Message>::FromError(
      MakeError("fire-and-forget method call expects a SOME/IP response"));
  }

  const auto request_correlation = CorrelationFromRequestId(request_id);
  if (call.correlation_id != 0U && call.correlation_id != request_correlation) {
    return core::Result<someip::Message>::FromError(
      MakeError("method call correlation does not match SOME/IP request id"));
  }

  someip::Message message{
    .header = {
      .message_id = {.service_id = mapping.service_id, .method_id = mapping.method_id},
      .request_id = request_id,
      .protocol_version = someip::kProtocolVersion,
      .interface_version = mapping.major_version,
      .message_type = someip::MessageType::kRequestNoReturn,
      .return_code = someip::ReturnCode::kOk,
    },
    .payload = call.payload,
  };

  if (!someip::FitsUdpPayload(message)) {
    return core::Result<someip::Message>::FromError(
      MakeError("fire-and-forget method request exceeds configured UDP payload limit"));
  }

  return core::Result<someip::Message>::FromValue(std::move(message));
}

core::Result<com::MethodCall> SomeIpBinding::DecodeFireAndForgetMethodRequest(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message) {
  auto validation = ValidateMethodMapping(mapping);
  if (!validation) {
    return core::Result<com::MethodCall>::FromError(validation.Error());
  }

  if (message.header.message_id.service_id != mapping.service_id ||
      message.header.message_id.method_id != mapping.method_id) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP message id does not match method mapping"));
  }

  if (message.header.protocol_version != someip::kProtocolVersion) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP protocol version is unsupported"));
  }

  if (message.header.interface_version != mapping.major_version) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP interface version does not match method mapping"));
  }

  if (message.header.message_type != someip::MessageType::kRequestNoReturn) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP method message is not a fire-and-forget request"));
  }

  if (message.header.return_code != someip::ReturnCode::kOk) {
    return core::Result<com::MethodCall>::FromError(
      MakeError("SOME/IP method request return code is not OK"));
  }

  return core::Result<com::MethodCall>::FromValue({
    .service = mapping.ara_service,
    .method_name = mapping.method_name,
    .payload = message.payload,
    .correlation_id = CorrelationFromRequestId(message.header.request_id),
    .expects_response = false,
  });
}

core::Result<someip::Message> SomeIpBinding::BuildMethodResponse(
  const SomeIpServiceMapping& mapping,
  const com::MethodResult& result,
  someip::RequestId request_id) {
  auto validation = ValidateMethodMapping(mapping);
  if (!validation) {
    return core::Result<someip::Message>::FromError(validation.Error());
  }

  if (!MatchesService(result.service, mapping.ara_service)) {
    return core::Result<someip::Message>::FromError(
      MakeError("method result service does not match SOME/IP mapping"));
  }

  if (result.method_name != mapping.method_name) {
    return core::Result<someip::Message>::FromError(
      MakeError("method result name does not match SOME/IP mapping"));
  }

  if (result.correlation_id == 0U ||
      result.correlation_id != CorrelationFromRequestId(request_id)) {
    return core::Result<someip::Message>::FromError(
      MakeError("method result correlation does not match SOME/IP request id"));
  }

  auto encoded_payload = EncodeMethodErrorPayload(result);
  if (!encoded_payload) {
    return core::Result<someip::Message>::FromError(encoded_payload.Error());
  }

  someip::Message message{
    .header = {
      .message_id = {.service_id = mapping.service_id, .method_id = mapping.method_id},
      .request_id = request_id,
      .protocol_version = someip::kProtocolVersion,
      .interface_version = mapping.major_version,
      .message_type = someip::MessageType::kResponse,
      .return_code = result.application_error ? someip::ReturnCode::kNotOk
                                              : someip::ReturnCode::kOk,
    },
    .payload = std::move(encoded_payload.Value()),
  };

  if (!someip::FitsUdpPayload(message)) {
    return core::Result<someip::Message>::FromError(
      MakeError("method response exceeds configured UDP payload limit"));
  }

  return core::Result<someip::Message>::FromValue(std::move(message));
}

core::Result<com::MethodResult> SomeIpBinding::DecodeMethodResponse(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message) {
  auto validation = ValidateMethodMapping(mapping);
  if (!validation) {
    return core::Result<com::MethodResult>::FromError(validation.Error());
  }

  if (message.header.message_id.service_id != mapping.service_id ||
      message.header.message_id.method_id != mapping.method_id) {
    return core::Result<com::MethodResult>::FromError(
      MakeError("SOME/IP message id does not match method mapping"));
  }

  if (message.header.protocol_version != someip::kProtocolVersion) {
    return core::Result<com::MethodResult>::FromError(
      MakeError("SOME/IP protocol version is unsupported"));
  }

  if (message.header.interface_version != mapping.major_version) {
    return core::Result<com::MethodResult>::FromError(
      MakeError("SOME/IP interface version does not match method mapping"));
  }

  if (message.header.message_type != someip::MessageType::kResponse) {
    return core::Result<com::MethodResult>::FromError(
      MakeError("SOME/IP method message is not a response"));
  }

  if (message.header.return_code != someip::ReturnCode::kOk &&
      message.header.return_code != someip::ReturnCode::kNotOk) {
    return core::Result<com::MethodResult>::FromError(
      MakeError("SOME/IP method response return code is unsupported"));
  }

  MethodErrorPayload decoded_error{
    .payload = message.payload,
    .error_domain = {},
    .error_code = 0U,
    .structured = false,
  };
  if (message.header.return_code == someip::ReturnCode::kNotOk) {
    auto decoded = DecodeMethodErrorPayload(message.payload);
    if (!decoded) {
      return core::Result<com::MethodResult>::FromError(decoded.Error());
    }
    decoded_error = std::move(decoded.Value());
  }

  return core::Result<com::MethodResult>::FromValue({
    .service = mapping.ara_service,
    .method_name = mapping.method_name,
    .payload = std::move(decoded_error.payload),
    .correlation_id = CorrelationFromRequestId(message.header.request_id),
    .application_error = message.header.return_code == someip::ReturnCode::kNotOk,
    .error_domain = std::move(decoded_error.error_domain),
    .error_code = decoded_error.error_code,
  });
}

core::Result<someip::Message> SomeIpBinding::BuildFieldGetterRequest(
  const SomeIpServiceMapping& mapping,
  someip::RequestId request_id) {
  auto validation = ValidateFieldGetterMapping(mapping);
  if (!validation) {
    return core::Result<someip::Message>::FromError(validation.Error());
  }

  return BuildFieldMessage(
    mapping,
    mapping.field_getter_id,
    someip::MessageType::kRequest,
    {},
    request_id,
    "field getter request exceeds configured UDP payload limit");
}

core::Result<com::FieldValue> SomeIpBinding::DecodeFieldGetterRequest(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message) {
  auto validation = ValidateFieldGetterMapping(mapping);
  if (!validation) {
    return core::Result<com::FieldValue>::FromError(validation.Error());
  }

  auto header = ValidateFieldMessageHeader(
    mapping,
    message,
    mapping.field_getter_id,
    someip::MessageType::kRequest);
  if (!header) {
    return core::Result<com::FieldValue>::FromError(header.Error());
  }

  if (!message.payload.empty()) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("SOME/IP field getter request payload is not empty"));
  }

  return core::Result<com::FieldValue>::FromValue({
    .service = mapping.ara_service,
    .field_name = mapping.field_name,
    .payload = {},
    .sequence = 0U,
  });
}

core::Result<someip::Message> SomeIpBinding::BuildFieldGetterResponse(
  const SomeIpServiceMapping& mapping,
  const com::FieldValue& value,
  someip::RequestId request_id) {
  auto mapping_validation = ValidateFieldGetterMapping(mapping);
  if (!mapping_validation) {
    return core::Result<someip::Message>::FromError(mapping_validation.Error());
  }

  auto value_validation = ValidateFieldValue(mapping, value);
  if (!value_validation) {
    return core::Result<someip::Message>::FromError(value_validation.Error());
  }

  return BuildFieldMessage(
    mapping,
    mapping.field_getter_id,
    someip::MessageType::kResponse,
    value.payload,
    request_id,
    "field getter response exceeds configured UDP payload limit");
}

core::Result<com::FieldValue> SomeIpBinding::DecodeFieldGetterResponse(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message) {
  auto validation = ValidateFieldGetterMapping(mapping);
  if (!validation) {
    return core::Result<com::FieldValue>::FromError(validation.Error());
  }

  return DecodeFieldValue(
    mapping,
    message,
    mapping.field_getter_id,
    someip::MessageType::kResponse);
}

core::Result<someip::Message> SomeIpBinding::BuildFieldSetterRequest(
  const SomeIpServiceMapping& mapping,
  const com::FieldValue& value,
  someip::RequestId request_id) {
  auto mapping_validation = ValidateFieldSetterMapping(mapping);
  if (!mapping_validation) {
    return core::Result<someip::Message>::FromError(mapping_validation.Error());
  }

  auto value_validation = ValidateFieldValue(mapping, value);
  if (!value_validation) {
    return core::Result<someip::Message>::FromError(value_validation.Error());
  }

  return BuildFieldMessage(
    mapping,
    mapping.field_setter_id,
    someip::MessageType::kRequest,
    value.payload,
    request_id,
    "field setter request exceeds configured UDP payload limit");
}

core::Result<com::FieldValue> SomeIpBinding::DecodeFieldSetterRequest(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message) {
  auto validation = ValidateFieldSetterMapping(mapping);
  if (!validation) {
    return core::Result<com::FieldValue>::FromError(validation.Error());
  }

  return DecodeFieldValue(
    mapping,
    message,
    mapping.field_setter_id,
    someip::MessageType::kRequest);
}

core::Result<someip::Message> SomeIpBinding::BuildFieldSetterResponse(
  const SomeIpServiceMapping& mapping,
  const com::FieldValue& value,
  someip::RequestId request_id) {
  auto mapping_validation = ValidateFieldSetterMapping(mapping);
  if (!mapping_validation) {
    return core::Result<someip::Message>::FromError(mapping_validation.Error());
  }

  auto value_validation = ValidateFieldValue(mapping, value);
  if (!value_validation) {
    return core::Result<someip::Message>::FromError(value_validation.Error());
  }

  return BuildFieldMessage(
    mapping,
    mapping.field_setter_id,
    someip::MessageType::kResponse,
    value.payload,
    request_id,
    "field setter response exceeds configured UDP payload limit");
}

core::Result<com::FieldValue> SomeIpBinding::DecodeFieldSetterResponse(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message) {
  auto validation = ValidateFieldSetterMapping(mapping);
  if (!validation) {
    return core::Result<com::FieldValue>::FromError(validation.Error());
  }

  return DecodeFieldValue(
    mapping,
    message,
    mapping.field_setter_id,
    someip::MessageType::kResponse);
}

core::Result<someip::Message> SomeIpBinding::BuildFieldNotification(
  const SomeIpServiceMapping& mapping,
  const com::FieldValue& value,
  someip::RequestId request_id) {
  auto mapping_validation = ValidateFieldNotifierMapping(mapping);
  if (!mapping_validation) {
    return core::Result<someip::Message>::FromError(mapping_validation.Error());
  }

  auto value_validation = ValidateFieldValue(mapping, value);
  if (!value_validation) {
    return core::Result<someip::Message>::FromError(value_validation.Error());
  }

  return BuildFieldMessage(
    mapping,
    mapping.field_notifier_id,
    someip::MessageType::kNotification,
    value.payload,
    request_id,
    "field notification exceeds configured UDP payload limit");
}

core::Result<com::FieldValue> SomeIpBinding::DecodeFieldNotification(
  const SomeIpServiceMapping& mapping,
  const someip::Message& message) {
  auto validation = ValidateFieldNotifierMapping(mapping);
  if (!validation) {
    return core::Result<com::FieldValue>::FromError(validation.Error());
  }

  return DecodeFieldValue(
    mapping,
    message,
    mapping.field_notifier_id,
    someip::MessageType::kNotification);
}

core::Result<std::size_t> SomeIpBinding::PublishEvent(
  const someip::UdpEndpoint& endpoint,
  someip::UdpEndpointAddress remote,
  const SomeIpServiceMapping& mapping,
  const com::EventSample& sample,
  someip::RequestId request_id) {
  auto message = BuildEventNotification(mapping, sample, request_id);
  if (!message) {
    return core::Result<std::size_t>::FromError(message.Error());
  }

  return endpoint.SendTo(message.Value(), std::move(remote));
}

core::Result<com::EventSample> SomeIpBinding::ReceiveEvent(
  const someip::UdpEndpoint& endpoint,
  const SomeIpServiceMapping& mapping) {
  auto datagram = endpoint.Receive();
  if (!datagram) {
    return core::Result<com::EventSample>::FromError(datagram.Error());
  }

  return DecodeEventNotification(mapping, datagram.Value().message);
}

core::Result<std::size_t> SomeIpBinding::PublishEvent(
  const someip::TcpConnection& connection,
  const SomeIpServiceMapping& mapping,
  const com::EventSample& sample,
  someip::RequestId request_id) {
  auto message = BuildEventNotification(mapping, sample, request_id);
  if (!message) {
    return core::Result<std::size_t>::FromError(message.Error());
  }

  return connection.Send(message.Value());
}

core::Result<com::EventSample> SomeIpBinding::ReceiveEvent(
  const someip::TcpConnection& connection,
  const SomeIpServiceMapping& mapping) {
  auto message = connection.Receive();
  if (!message) {
    return core::Result<com::EventSample>::FromError(message.Error());
  }

  return DecodeEventNotification(mapping, message.Value());
}

}  // namespace openautosar::someip_binding
