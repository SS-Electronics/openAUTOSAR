// SPDX-License-Identifier: MIT

#include "openautosar/com/service_registry.h"
#include "openautosar/someip/message.h"
#include "openautosar/someip/service_discovery.h"
#include "openautosar/someip/tcp_stream.h"
#include "openautosar/someip/udp_endpoint.h"
#include "openautosar/someip_binding/someip_binding.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

}  // namespace

int main() {
  namespace com = openautosar::com;
  namespace sd = openautosar::someip::sd;
  using openautosar::someip::MessageType;
  using openautosar::someip::RequestId;
  using openautosar::someip::SerializeMessage;
  using openautosar::someip::TcpConnection;
  using openautosar::someip::TcpServer;
  using openautosar::someip::UdpEndpoint;
  using openautosar::someip_binding::SomeIpBinding;
  using openautosar::someip_binding::SomeIpServiceMapping;

  constexpr com::ServiceIdentifier ultrasonic_service{
    .interface_id = 0x0A500001U,
    .instance_id = 0x00000001U,
    .major_version = 1U,
    .minor_version = 0U,
  };

  const SomeIpServiceMapping mapping{
    .ara_service = ultrasonic_service,
    .service_id = 0xA501U,
    .instance_id = 0x0001U,
    .event_id = 0x8001U,
    .event_name = "DistanceSample",
    .eventgroup_id = 0x0100U,
    .method_id = 0x0421U,
    .method_name = "GetDistanceStatistics",
    .field_getter_id = 0U,
    .field_setter_id = 0U,
    .field_notifier_id = 0U,
    .field_name = "",
    .major_version = 1U,
    .minor_version = 0U,
    .ttl_seconds = 3U,
  };

  com::ServiceRegistry registry;
  const auto offer = registry.OfferService({
    .service = ultrasonic_service,
    .process_identity = "someip-binding-provider",
    .machine_identity = "qemux86-64",
    .endpoint = {.binding = com::Binding::kSomeIp, .address = "127.0.0.1", .port = 30501U},
    .ttl_ms = 2'500U,
    .access_policy = "unit-test",
    .deployment_provenance = "someip-binding-test",
  });
  Require(offer.HasValue(), "SOME/IP service offer failed");

  const RequestId sd_request{.client_id = 0x0001U, .session_id = 0x0001U};
  const auto offer_message = SomeIpBinding::BuildOffer(offer.Value(), mapping, sd_request);
  Require(offer_message.HasValue(), "SOME/IP binding offer message build failed");

  const auto offer_sd = sd::ExtractServiceDiscoveryMessage(offer_message.Value());
  Require(offer_sd.HasValue(), "SOME/IP binding offer SD extraction failed");
  Require(offer_sd.Value().entries.size() == 1U, "offer SD entry count changed");
  Require(
    offer_sd.Value().entries[0U].type == sd::EntryType::kOfferService,
    "offer SD type changed");
  Require(
    offer_sd.Value().entries[0U].service_id == mapping.service_id,
    "offer service id changed");
  Require(
    offer_sd.Value().entries[0U].instance_id == mapping.instance_id,
    "offer instance id changed");
  Require(offer_sd.Value().entries[0U].ttl == 3U, "offer TTL conversion changed");

  const auto find_message = SomeIpBinding::BuildFind(
    mapping,
    {.client_id = 0x0001U, .session_id = 0x0002U});
  Require(find_message.HasValue(), "SOME/IP binding find message build failed");
  const auto find_sd = sd::ExtractServiceDiscoveryMessage(find_message.Value());
  Require(find_sd.HasValue(), "SOME/IP binding find SD extraction failed");
  Require(find_sd.Value().entries[0U].type == sd::EntryType::kFindService, "find SD type changed");
  Require(find_sd.Value().entries[0U].ttl == 0U, "find SD TTL changed");

  const auto subscribe_message = SomeIpBinding::BuildSubscribe(
    mapping,
    {.client_id = 0x0001U, .session_id = 0x0003U});
  Require(subscribe_message.HasValue(), "SOME/IP binding subscribe message build failed");
  const auto subscribe_sd = sd::ExtractServiceDiscoveryMessage(subscribe_message.Value());
  Require(subscribe_sd.HasValue(), "SOME/IP binding subscribe SD extraction failed");
  Require(
    subscribe_sd.Value().entries[0U].type == sd::EntryType::kSubscribeEventgroup,
    "subscribe SD type changed");
  Require(
    subscribe_sd.Value().entries[0U].eventgroup_id == mapping.eventgroup_id,
    "subscribe eventgroup changed");

  const auto subscribe_ack = SomeIpBinding::BuildSubscribeAck(
    mapping,
    {.client_id = 0x0001U, .session_id = 0x0004U});
  Require(subscribe_ack.HasValue(), "SOME/IP binding subscribe ack build failed");
  const auto subscribe_ack_sd = sd::ExtractServiceDiscoveryMessage(subscribe_ack.Value());
  Require(subscribe_ack_sd.HasValue(), "SOME/IP binding subscribe ack extraction failed");
  Require(
    subscribe_ack_sd.Value().entries[0U].type == sd::EntryType::kSubscribeEventgroupAck,
    "subscribe ack SD type changed");

  const auto subscription = registry.Subscribe(ultrasonic_service, "DistanceSample", 2U);
  Require(subscription.HasValue(), "registry subscription failed");

  const com::EventSample sample{
    .service = ultrasonic_service,
    .event_name = "DistanceSample",
    .payload = {0x01U, 0x2CU, 0x62U, 0x10U},
  };

  const auto event_message = SomeIpBinding::BuildEventNotification(
    mapping,
    sample,
    {.client_id = 0xA501U, .session_id = 0x0001U});
  Require(event_message.HasValue(), "SOME/IP event notification build failed");
  Require(
    event_message.Value().header.message_id.service_id == mapping.service_id,
    "event service id changed");
  Require(
    event_message.Value().header.message_id.method_id == mapping.event_id,
    "event id changed");
  Require(
    event_message.Value().header.message_type == MessageType::kNotification,
    "event type changed");

  const auto event_bytes = SerializeMessage(event_message.Value());
  Require(event_bytes.HasValue(), "SOME/IP event notification serialization failed");

  const auto decoded_sample =
    SomeIpBinding::DecodeEventNotification(mapping, event_message.Value());
  Require(decoded_sample.HasValue(), "SOME/IP event notification decode failed");
  Require(decoded_sample.Value().service == ultrasonic_service, "decoded service changed");
  Require(decoded_sample.Value().event_name == "DistanceSample", "decoded event name changed");
  Require(decoded_sample.Value().payload == sample.payload, "decoded event payload changed");

  auto wrong_type = event_message.Value();
  wrong_type.header.message_type = MessageType::kResponse;
  Require(
    !SomeIpBinding::DecodeEventNotification(mapping, wrong_type).HasValue(),
    "SOME/IP response was accepted as event notification");

  auto wrong_event = event_message.Value();
  wrong_event.header.message_id.method_id = 0x8002U;
  Require(
    !SomeIpBinding::DecodeEventNotification(mapping, wrong_event).HasValue(),
    "wrong SOME/IP event id was accepted");

  const RequestId method_request_id{.client_id = 0x0A51U, .session_id = 0x0102U};
  constexpr std::uint64_t method_correlation{0x0A510102ULL};
  const com::MethodCall method_call{
    .service = ultrasonic_service,
    .method_name = "GetDistanceStatistics",
    .payload = {0x00U, 0x64U},
    .correlation_id = method_correlation,
  };

  const auto method_request = SomeIpBinding::BuildMethodRequest(
    mapping,
    method_call,
    method_request_id);
  Require(method_request.HasValue(), "SOME/IP method request build failed");
  Require(
    method_request.Value().header.message_id.method_id == mapping.method_id,
    "SOME/IP method id changed");
  Require(
    method_request.Value().header.message_type == MessageType::kRequest,
    "SOME/IP method request type changed");
  const auto method_request_bytes = SerializeMessage(method_request.Value());
  Require(method_request_bytes.HasValue(), "SOME/IP method request serialization failed");

  const auto decoded_method_call =
    SomeIpBinding::DecodeMethodRequest(mapping, method_request.Value());
  Require(decoded_method_call.HasValue(), "SOME/IP method request decode failed");
  Require(
    decoded_method_call.Value() == method_call,
    "SOME/IP decoded method call changed");

  const com::MethodResult method_result{
    .service = ultrasonic_service,
    .method_name = "GetDistanceStatistics",
    .payload = {0x01U, 0x2CU},
    .correlation_id = method_correlation,
    .application_error = false,
    .error_domain = {},
    .error_code = 0U,
  };
  const auto method_response = SomeIpBinding::BuildMethodResponse(
    mapping,
    method_result,
    method_request_id);
  Require(method_response.HasValue(), "SOME/IP method response build failed");
  Require(
    method_response.Value().header.message_type == MessageType::kResponse,
    "SOME/IP method response type changed");

  const auto decoded_method_result =
    SomeIpBinding::DecodeMethodResponse(mapping, method_response.Value());
  Require(decoded_method_result.HasValue(), "SOME/IP method response decode failed");
  Require(
    decoded_method_result.Value() == method_result,
    "SOME/IP decoded method result changed");

  com::MethodResult method_error = method_result;
  method_error.payload = {0xEEU};
  method_error.application_error = true;
  method_error.error_domain = "UltrasonicDistanceService.GetDistanceStatistics";
  method_error.error_code = 1U;
  const auto method_error_response = SomeIpBinding::BuildMethodResponse(
    mapping,
    method_error,
    method_request_id);
  Require(method_error_response.HasValue(), "SOME/IP method error response build failed");
  const auto decoded_method_error =
    SomeIpBinding::DecodeMethodResponse(mapping, method_error_response.Value());
  Require(decoded_method_error.HasValue(), "SOME/IP method error response decode failed");
  Require(
    decoded_method_error.Value().application_error,
    "SOME/IP method application error flag changed");
  Require(
    decoded_method_error.Value().payload == std::vector<std::uint8_t>({0xEEU}),
    "SOME/IP method application error payload changed");
  Require(
    decoded_method_error.Value().error_domain ==
      "UltrasonicDistanceService.GetDistanceStatistics",
    "SOME/IP method application error domain changed");
  Require(
    decoded_method_error.Value().error_code == 1U,
    "SOME/IP method application error code changed");

  auto wrong_method_type = method_request.Value();
  wrong_method_type.header.message_type = MessageType::kNotification;
  Require(
    !SomeIpBinding::DecodeMethodRequest(mapping, wrong_method_type).HasValue(),
    "SOME/IP notification was accepted as method request");
  com::MethodResult wrong_correlation_result = method_result;
  wrong_correlation_result.correlation_id = 0x01020304ULL;
  Require(
    !SomeIpBinding::BuildMethodResponse(
       mapping,
       wrong_correlation_result,
       method_request_id)
       .HasValue(),
    "SOME/IP method response accepted mismatched correlation");

  SomeIpServiceMapping fire_and_forget_mapping = mapping;
  fire_and_forget_mapping.method_id = 0x0422U;
  fire_and_forget_mapping.method_name = "ResetCalibration";
  const RequestId fire_and_forget_request_id{.client_id = 0x0A51U, .session_id = 0x0103U};
  constexpr std::uint64_t fire_and_forget_correlation{0x0A510103ULL};
  const com::MethodCall fire_and_forget_call{
    .service = ultrasonic_service,
    .method_name = "ResetCalibration",
    .payload = {0x02U},
    .correlation_id = fire_and_forget_correlation,
    .expects_response = false,
  };
  const auto fire_and_forget_request = SomeIpBinding::BuildFireAndForgetMethodRequest(
    fire_and_forget_mapping,
    fire_and_forget_call,
    fire_and_forget_request_id);
  Require(
    fire_and_forget_request.HasValue(),
    "SOME/IP fire-and-forget method request build failed");
  Require(
    fire_and_forget_request.Value().header.message_type == MessageType::kRequestNoReturn,
    "SOME/IP fire-and-forget request type changed");
  const auto decoded_fire_and_forget_call =
    SomeIpBinding::DecodeFireAndForgetMethodRequest(
      fire_and_forget_mapping,
      fire_and_forget_request.Value());
  Require(
    decoded_fire_and_forget_call.HasValue(),
    "SOME/IP fire-and-forget method request decode failed");
  Require(
    decoded_fire_and_forget_call.Value() == fire_and_forget_call,
    "SOME/IP decoded fire-and-forget method call changed");
  Require(
    !SomeIpBinding::BuildMethodRequest(
       fire_and_forget_mapping,
       fire_and_forget_call,
       fire_and_forget_request_id)
       .HasValue(),
    "SOME/IP response-expected builder accepted fire-and-forget call");
  com::MethodCall response_expected_fire_call = fire_and_forget_call;
  response_expected_fire_call.expects_response = true;
  Require(
    !SomeIpBinding::BuildFireAndForgetMethodRequest(
       fire_and_forget_mapping,
       response_expected_fire_call,
       fire_and_forget_request_id)
       .HasValue(),
    "SOME/IP fire-and-forget builder accepted response-expected call");
  Require(
    !SomeIpBinding::DecodeMethodRequest(
       fire_and_forget_mapping,
       fire_and_forget_request.Value())
       .HasValue(),
    "SOME/IP no-return request decoded as response-expected method");

  SomeIpServiceMapping field_mapping = mapping;
  field_mapping.field_getter_id = 0x0521U;
  field_mapping.field_setter_id = 0x0522U;
  field_mapping.field_notifier_id = 0x8521U;
  field_mapping.field_name = "CalibrationMode";
  const RequestId field_request_id{.client_id = 0x0A51U, .session_id = 0x0104U};
  const com::FieldValue field_value{
    .service = ultrasonic_service,
    .field_name = "CalibrationMode",
    .payload = {0x03U},
    .sequence = 7U,
  };

  const auto field_getter_request =
    SomeIpBinding::BuildFieldGetterRequest(field_mapping, field_request_id);
  Require(field_getter_request.HasValue(), "SOME/IP field getter request build failed");
  Require(
    field_getter_request.Value().header.message_id.method_id == field_mapping.field_getter_id,
    "SOME/IP field getter id changed");
  Require(
    field_getter_request.Value().header.message_type == MessageType::kRequest,
    "SOME/IP field getter request type changed");
  const auto decoded_field_getter_request =
    SomeIpBinding::DecodeFieldGetterRequest(field_mapping, field_getter_request.Value());
  Require(
    decoded_field_getter_request.HasValue(),
    "SOME/IP field getter request decode failed");
  Require(
    decoded_field_getter_request.Value().service == ultrasonic_service,
    "SOME/IP decoded field getter service changed");
  Require(
    decoded_field_getter_request.Value().field_name == "CalibrationMode",
    "SOME/IP decoded field getter name changed");
  Require(
    decoded_field_getter_request.Value().payload.empty(),
    "SOME/IP decoded field getter request carried payload");

  const auto field_getter_response = SomeIpBinding::BuildFieldGetterResponse(
    field_mapping,
    field_value,
    field_request_id);
  Require(field_getter_response.HasValue(), "SOME/IP field getter response build failed");
  const auto decoded_field_getter_response =
    SomeIpBinding::DecodeFieldGetterResponse(field_mapping, field_getter_response.Value());
  Require(
    decoded_field_getter_response.HasValue(),
    "SOME/IP field getter response decode failed");
  Require(
    decoded_field_getter_response.Value().payload == field_value.payload,
    "SOME/IP decoded field getter response payload changed");

  const auto field_setter_request = SomeIpBinding::BuildFieldSetterRequest(
    field_mapping,
    field_value,
    field_request_id);
  Require(field_setter_request.HasValue(), "SOME/IP field setter request build failed");
  Require(
    field_setter_request.Value().header.message_id.method_id == field_mapping.field_setter_id,
    "SOME/IP field setter id changed");
  const auto decoded_field_setter_request =
    SomeIpBinding::DecodeFieldSetterRequest(field_mapping, field_setter_request.Value());
  Require(
    decoded_field_setter_request.HasValue(),
    "SOME/IP field setter request decode failed");
  Require(
    decoded_field_setter_request.Value().payload == field_value.payload,
    "SOME/IP decoded field setter request payload changed");

  const auto field_setter_response = SomeIpBinding::BuildFieldSetterResponse(
    field_mapping,
    field_value,
    field_request_id);
  Require(field_setter_response.HasValue(), "SOME/IP field setter response build failed");
  const auto decoded_field_setter_response =
    SomeIpBinding::DecodeFieldSetterResponse(field_mapping, field_setter_response.Value());
  Require(
    decoded_field_setter_response.HasValue(),
    "SOME/IP field setter response decode failed");
  Require(
    decoded_field_setter_response.Value().field_name == field_value.field_name,
    "SOME/IP decoded field setter response name changed");

  const auto field_notification = SomeIpBinding::BuildFieldNotification(
    field_mapping,
    field_value,
    {.client_id = 0xA501U, .session_id = 0x0005U});
  Require(field_notification.HasValue(), "SOME/IP field notification build failed");
  Require(
    field_notification.Value().header.message_id.method_id == field_mapping.field_notifier_id,
    "SOME/IP field notifier id changed");
  Require(
    field_notification.Value().header.message_type == MessageType::kNotification,
    "SOME/IP field notification type changed");
  const auto decoded_field_notification =
    SomeIpBinding::DecodeFieldNotification(field_mapping, field_notification.Value());
  Require(
    decoded_field_notification.HasValue(),
    "SOME/IP field notification decode failed");
  Require(
    decoded_field_notification.Value().payload == field_value.payload,
    "SOME/IP decoded field notification payload changed");

  auto invalid_notifier = field_mapping;
  invalid_notifier.field_notifier_id = 0x0523U;
  Require(
    !SomeIpBinding::BuildFieldNotification(
       invalid_notifier,
       field_value,
       {.client_id = 0xA501U, .session_id = 0x0006U})
       .HasValue(),
    "SOME/IP field notifier outside event range was accepted");

  const auto receiver = UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  const auto sender = UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  Require(receiver.HasValue() && sender.HasValue(), "SOME/IP binding UDP endpoints did not bind");

  const auto receiver_address = receiver.Value().LocalAddress();
  Require(receiver_address.HasValue(), "SOME/IP binding receiver address unavailable");

  const auto sent = SomeIpBinding::PublishEvent(
    sender.Value(),
    receiver_address.Value(),
    mapping,
    sample,
    {.client_id = 0xA501U, .session_id = 0x0002U});
  Require(sent.HasValue(), "SOME/IP binding UDP event publish failed");
  Require(sent.Value() == event_bytes.Value().size(), "SOME/IP binding UDP send size changed");

  const auto received_sample = SomeIpBinding::ReceiveEvent(receiver.Value(), mapping);
  Require(received_sample.HasValue(), "SOME/IP binding UDP event receive failed");

  const auto published = registry.Publish(received_sample.Value());
  Require(published.HasValue(), "received SOME/IP sample did not publish to registry");
  const auto delivered = registry.Poll(subscription.Value().id);
  Require(delivered.HasValue(), "registry did not deliver received SOME/IP sample");
  Require(
    delivered.Value().sequence == published.Value(),
    "registry sequence changed after SOME/IP receive");
  Require(
    delivered.Value().payload == sample.payload,
    "registry payload changed after SOME/IP receive");

  auto tcp_server = TcpServer::Listen({.host = "127.0.0.1", .port = 0U});
  Require(tcp_server.HasValue(), "SOME/IP binding TCP server did not listen");

  const auto tcp_address = tcp_server.Value().LocalAddress();
  Require(tcp_address.HasValue(), "SOME/IP binding TCP address unavailable");

  auto tcp_client = TcpConnection::Connect(tcp_address.Value());
  Require(tcp_client.HasValue(), "SOME/IP binding TCP client did not connect");

  auto tcp_peer = tcp_server.Value().Accept();
  Require(tcp_peer.HasValue(), "SOME/IP binding TCP server did not accept client");

  const auto tcp_sent = SomeIpBinding::PublishEvent(
    tcp_client.Value(),
    mapping,
    sample,
    {.client_id = 0xA501U, .session_id = 0x0003U});
  Require(tcp_sent.HasValue(), "SOME/IP binding TCP event publish failed");
  Require(tcp_sent.Value() == event_bytes.Value().size(), "SOME/IP binding TCP send size changed");

  const auto tcp_received_sample = SomeIpBinding::ReceiveEvent(tcp_peer.Value(), mapping);
  Require(tcp_received_sample.HasValue(), "SOME/IP binding TCP event receive failed");
  Require(
    tcp_received_sample.Value().payload == sample.payload,
    "SOME/IP binding TCP payload changed");

  SomeIpServiceMapping invalid_mapping = mapping;
  invalid_mapping.eventgroup_id = 0U;
  Require(
    !SomeIpBinding::BuildSubscribe(invalid_mapping, sd_request).HasValue(),
    "invalid SOME/IP eventgroup mapping was accepted");

  SomeIpServiceMapping invalid_method_mapping = mapping;
  invalid_method_mapping.method_id = 0x8002U;
  Require(
    !SomeIpBinding::BuildMethodRequest(
       invalid_method_mapping,
       method_call,
       method_request_id)
       .HasValue(),
    "SOME/IP event-range method id was accepted");

  const auto wrong_sample = SomeIpBinding::BuildEventNotification(
    mapping,
    {
      .service = ultrasonic_service,
      .event_name = "OtherEvent",
      .payload = {0x01U},
    },
    sd_request);
  Require(!wrong_sample.HasValue(), "wrong ara::com event name was accepted");

  return 0;
}
