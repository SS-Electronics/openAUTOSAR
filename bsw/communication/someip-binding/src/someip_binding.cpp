// SPDX-License-Identifier: MIT

#include "openautosar/someip_binding/someip_binding.h"

#include "openautosar/someip/service_discovery.h"

#include <algorithm>
#include <utility>

namespace openautosar::someip_binding {
namespace {

inline constexpr std::uint32_t kMaxSdTtlSeconds{0x00FFFFFFU};

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"someip-binding", message};
}

[[nodiscard]] bool MatchesService(
  const com::ServiceIdentifier& left,
  const com::ServiceIdentifier& right) noexcept {
  return left == right;
}

[[nodiscard]] core::Result<bool> ValidateMapping(const SomeIpServiceMapping& mapping) {
  if (mapping.ara_service.interface_id == 0U || mapping.ara_service.instance_id == 0U) {
    return core::Result<bool>::FromError(MakeError("ARA service identifier is invalid"));
  }

  if (mapping.ara_service.major_version == 0U || mapping.major_version == 0U) {
    return core::Result<bool>::FromError(MakeError("service major version is invalid"));
  }

  if (mapping.service_id == 0U || mapping.instance_id == 0U) {
    return core::Result<bool>::FromError(MakeError("SOME/IP service or instance id is invalid"));
  }

  if (mapping.event_id == 0U || mapping.eventgroup_id == 0U) {
    return core::Result<bool>::FromError(MakeError("SOME/IP event or eventgroup id is invalid"));
  }

  if (mapping.event_name.empty()) {
    return core::Result<bool>::FromError(MakeError("event name is empty"));
  }

  if (mapping.ttl_seconds > kMaxSdTtlSeconds) {
    return core::Result<bool>::FromError(MakeError("SOME/IP-SD TTL exceeds 24-bit field"));
  }

  return core::Result<bool>::FromValue(true);
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
  auto validation = ValidateMapping(mapping);
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
  auto validation = ValidateMapping(mapping);
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
  auto validation = ValidateMapping(mapping);
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
