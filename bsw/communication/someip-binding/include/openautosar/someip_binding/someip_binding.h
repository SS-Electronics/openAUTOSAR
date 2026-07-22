// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/com/service_registry.h"
#include "openautosar/core/result.h"
#include "openautosar/someip/message.h"
#include "openautosar/someip/tcp_stream.h"
#include "openautosar/someip/udp_endpoint.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace openautosar::someip_binding {

struct SomeIpServiceMapping final {
  com::ServiceIdentifier ara_service{};
  std::uint16_t service_id{0U};
  std::uint16_t instance_id{0U};
  std::uint16_t event_id{0U};
  std::string event_name;
  std::uint16_t eventgroup_id{0U};
  std::uint16_t method_id{0U};
  std::string method_name;
  std::uint16_t field_getter_id{0U};
  std::uint16_t field_setter_id{0U};
  std::uint16_t field_notifier_id{0U};
  std::string field_name;
  std::uint8_t major_version{1U};
  std::uint32_t minor_version{0U};
  std::uint32_t ttl_seconds{3U};

  friend bool operator==(const SomeIpServiceMapping&, const SomeIpServiceMapping&) = default;
};

class SomeIpBinding final {
public:
  [[nodiscard]] static core::Result<someip::Message> BuildOffer(
    const com::ServiceOffer& offer,
    const SomeIpServiceMapping& mapping,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<someip::Message> BuildFind(
    const SomeIpServiceMapping& mapping,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<someip::Message> BuildSubscribe(
    const SomeIpServiceMapping& mapping,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<someip::Message> BuildSubscribeAck(
    const SomeIpServiceMapping& mapping,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<someip::Message> BuildEventNotification(
    const SomeIpServiceMapping& mapping,
    const com::EventSample& sample,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::EventSample> DecodeEventNotification(
    const SomeIpServiceMapping& mapping,
    const someip::Message& message);

  [[nodiscard]] static core::Result<someip::Message> BuildMethodRequest(
    const SomeIpServiceMapping& mapping,
    const com::MethodCall& call,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::MethodCall> DecodeMethodRequest(
    const SomeIpServiceMapping& mapping,
    const someip::Message& message);

  [[nodiscard]] static core::Result<someip::Message> BuildFireAndForgetMethodRequest(
    const SomeIpServiceMapping& mapping,
    const com::MethodCall& call,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::MethodCall> DecodeFireAndForgetMethodRequest(
    const SomeIpServiceMapping& mapping,
    const someip::Message& message);

  [[nodiscard]] static core::Result<someip::Message> BuildMethodResponse(
    const SomeIpServiceMapping& mapping,
    const com::MethodResult& result,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::MethodResult> DecodeMethodResponse(
    const SomeIpServiceMapping& mapping,
    const someip::Message& message);

  [[nodiscard]] static core::Result<someip::Message> BuildFieldGetterRequest(
    const SomeIpServiceMapping& mapping,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::FieldValue> DecodeFieldGetterRequest(
    const SomeIpServiceMapping& mapping,
    const someip::Message& message);

  [[nodiscard]] static core::Result<someip::Message> BuildFieldGetterResponse(
    const SomeIpServiceMapping& mapping,
    const com::FieldValue& value,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::FieldValue> DecodeFieldGetterResponse(
    const SomeIpServiceMapping& mapping,
    const someip::Message& message);

  [[nodiscard]] static core::Result<someip::Message> BuildFieldSetterRequest(
    const SomeIpServiceMapping& mapping,
    const com::FieldValue& value,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::FieldValue> DecodeFieldSetterRequest(
    const SomeIpServiceMapping& mapping,
    const someip::Message& message);

  [[nodiscard]] static core::Result<someip::Message> BuildFieldSetterResponse(
    const SomeIpServiceMapping& mapping,
    const com::FieldValue& value,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::FieldValue> DecodeFieldSetterResponse(
    const SomeIpServiceMapping& mapping,
    const someip::Message& message);

  [[nodiscard]] static core::Result<someip::Message> BuildFieldNotification(
    const SomeIpServiceMapping& mapping,
    const com::FieldValue& value,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::FieldValue> DecodeFieldNotification(
    const SomeIpServiceMapping& mapping,
    const someip::Message& message);

  [[nodiscard]] static core::Result<std::size_t> PublishEvent(
    const someip::UdpEndpoint& endpoint,
    someip::UdpEndpointAddress remote,
    const SomeIpServiceMapping& mapping,
    const com::EventSample& sample,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::EventSample> ReceiveEvent(
    const someip::UdpEndpoint& endpoint,
    const SomeIpServiceMapping& mapping);

  [[nodiscard]] static core::Result<std::size_t> PublishEvent(
    const someip::TcpConnection& connection,
    const SomeIpServiceMapping& mapping,
    const com::EventSample& sample,
    someip::RequestId request_id);

  [[nodiscard]] static core::Result<com::EventSample> ReceiveEvent(
    const someip::TcpConnection& connection,
    const SomeIpServiceMapping& mapping);
};

}  // namespace openautosar::someip_binding
