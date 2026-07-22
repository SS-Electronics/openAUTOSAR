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
