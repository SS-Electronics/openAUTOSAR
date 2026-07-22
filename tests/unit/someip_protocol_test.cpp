// SPDX-License-Identifier: MIT

#include "openautosar/someip/message.h"
#include "openautosar/someip/request_correlation.h"
#include "openautosar/someip/service_discovery.h"
#include "openautosar/someip/tcp_stream.h"
#include "openautosar/someip/udp_endpoint.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

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
  using namespace openautosar::someip;

  const Message request{
    .header = {
      .message_id = {.service_id = 0x1234U, .method_id = 0x0421U},
      .request_id = {.client_id = 0x0001U, .session_id = 0x0002U},
      .protocol_version = kProtocolVersion,
      .interface_version = 1U,
      .message_type = MessageType::kRequest,
      .return_code = ReturnCode::kOk,
    },
    .payload = {0xDEU, 0xADU, 0xBEU, 0xEFU},
  };

  const std::vector<std::uint8_t> expected_request{
    0x12U, 0x34U, 0x04U, 0x21U, 0x00U, 0x00U, 0x00U, 0x0CU,
    0x00U, 0x01U, 0x00U, 0x02U, 0x01U, 0x01U, 0x00U, 0x00U,
    0xDEU, 0xADU, 0xBEU, 0xEFU,
  };

  const auto encoded_request = SerializeMessage(request);
  Require(encoded_request.HasValue(), "SOME/IP request did not serialize");
  Require(encoded_request.Value() == expected_request, "SOME/IP request golden vector changed");

  const auto decoded_request = DeserializeMessage(expected_request);
  Require(decoded_request.HasValue(), "SOME/IP request did not deserialize");
  Require(decoded_request.Value() == request, "SOME/IP request roundtrip changed");

  auto length_mismatch = expected_request;
  length_mismatch[7U] = 0x0DU;
  Require(!DeserializeMessage(length_mismatch).HasValue(), "SOME/IP length mismatch was accepted");

  auto bad_protocol = expected_request;
  bad_protocol[12U] = 0x02U;
  Require(
    !DeserializeMessage(bad_protocol).HasValue(),
    "SOME/IP protocol version mismatch was accepted");

  Message oversize = request;
  oversize.payload.assign(kMaxUdpPayloadSize, 0xAAU);
  Require(!SerializeMessage(oversize).HasValue(), "oversized UDP SOME/IP message was accepted");

  const sd::ServiceDiscoveryMessage discovery{
    .reboot_session = 0x00000001U,
    .entries = {
      {
        .type = sd::EntryType::kOfferService,
        .service_id = 0xA501U,
        .instance_id = 0x0001U,
        .major_version = 1U,
        .ttl = 3U,
        .minor_version = 0U,
        .eventgroup_id = 0U,
      },
      {
        .type = sd::EntryType::kSubscribeEventgroup,
        .service_id = 0xA501U,
        .instance_id = 0x0001U,
        .major_version = 1U,
        .ttl = 3U,
        .minor_version = 0U,
        .eventgroup_id = 0x0100U,
      },
    },
  };

  const std::vector<std::uint8_t> expected_sd_payload{
    0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x20U,
    0x01U, 0x00U, 0xA5U, 0x01U, 0x00U, 0x01U, 0x01U, 0x00U,
    0x00U, 0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x06U, 0x00U, 0xA5U, 0x01U, 0x00U, 0x01U, 0x01U, 0x00U,
    0x00U, 0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U,
  };

  const auto encoded_sd_payload = sd::SerializeServiceDiscoveryPayload(discovery);
  Require(encoded_sd_payload.HasValue(), "SOME/IP-SD payload did not serialize");
  Require(
    encoded_sd_payload.Value() == expected_sd_payload,
    "SOME/IP-SD payload golden vector changed");

  const auto sd_message = sd::BuildServiceDiscoveryMessage(
    discovery,
    {.client_id = 0x0001U, .session_id = 0x0010U});
  Require(sd_message.HasValue(), "SOME/IP-SD message build failed");
  Require(
    sd_message.Value().header.message_id == sd::kServiceDiscoveryMessageId,
    "SOME/IP-SD id changed");
  Require(
    sd_message.Value().header.message_type == MessageType::kNotification,
    "SOME/IP-SD type changed");

  const auto encoded_sd_message = SerializeMessage(sd_message.Value());
  Require(encoded_sd_message.HasValue(), "SOME/IP-SD message serialization failed");
  const auto decoded_sd_message = DeserializeMessage(encoded_sd_message.Value());
  Require(decoded_sd_message.HasValue(), "SOME/IP-SD message deserialization failed");

  const auto extracted = sd::ExtractServiceDiscoveryMessage(decoded_sd_message.Value());
  Require(extracted.HasValue(), "SOME/IP-SD extraction failed");
  Require(extracted.Value() == discovery, "SOME/IP-SD roundtrip changed");

  auto malformed_sd_payload = expected_sd_payload;
  malformed_sd_payload[8U] = 0xFFU;
  Require(
    !sd::DeserializeServiceDiscoveryPayload(malformed_sd_payload).HasValue(),
    "unknown SOME/IP-SD entry type was accepted");

  RequestCorrelator correlator(2U);
  const auto request_id = correlator.Allocate(
    0x0001U,
    {.service_id = 0x1234U, .method_id = 0x0421U},
    1'000U,
    500U);
  Require(request_id.HasValue(), "request correlation allocation failed");
  Require(correlator.PendingCount() == 1U, "pending request count changed");

  const auto wrong_message = correlator.Complete(
    request_id.Value(),
    {.service_id = 0x1234U, .method_id = 0x9999U},
    1'100U);
  Require(!wrong_message.HasValue(), "wrong SOME/IP message id completed correlation");

  const auto completed = correlator.Complete(
    request_id.Value(),
    {.service_id = 0x1234U, .method_id = 0x0421U},
    1'100U);
  Require(completed.HasValue(), "request correlation completion failed");
  Require(correlator.PendingCount() == 0U, "completed request remained pending");

  const auto expired_request = correlator.Allocate(
    0x0001U,
    {.service_id = 0x1234U, .method_id = 0x0001U},
    2'000U,
    10U);
  Require(expired_request.HasValue(), "request correlation allocation for expiry failed");
  correlator.Expire(3'000U);
  Require(correlator.PendingCount() == 0U, "expired request remained pending");

  auto receiver = UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  auto sender = UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  Require(receiver.HasValue() && sender.HasValue(), "UDP loopback endpoints did not bind");

  const auto receiver_address = receiver.Value().LocalAddress();
  Require(receiver_address.HasValue(), "receiver local UDP address unavailable");

  const auto sent = sender.Value().SendTo(request, receiver_address.Value());
  Require(sent.HasValue(), "UDP SOME/IP send failed");
  Require(sent.Value() == expected_request.size(), "UDP SOME/IP send size changed");

  const auto received = receiver.Value().Receive();
  Require(received.HasValue(), "UDP SOME/IP receive failed");
  Require(received.Value().message == request, "UDP SOME/IP loopback message changed");
  Require(received.Value().remote.port != 0U, "UDP remote endpoint port was not captured");

  const auto invalid_bind = UdpEndpoint::Bind({.host = "not-an-ip-address", .port = 0U});
  Require(!invalid_bind.HasValue(), "invalid UDP bind address was accepted");

  auto tcp_server = TcpServer::Listen({.host = "127.0.0.1", .port = 0U});
  Require(tcp_server.HasValue(), "TCP SOME/IP server did not listen");

  const auto tcp_address = tcp_server.Value().LocalAddress();
  Require(tcp_address.HasValue(), "TCP SOME/IP server address unavailable");

  auto tcp_client = TcpConnection::Connect(tcp_address.Value());
  Require(tcp_client.HasValue(), "TCP SOME/IP client did not connect");

  auto tcp_peer = tcp_server.Value().Accept();
  Require(tcp_peer.HasValue(), "TCP SOME/IP server did not accept client");

  const auto tcp_sent = tcp_client.Value().Send(request);
  Require(tcp_sent.HasValue(), "TCP SOME/IP send failed");
  Require(tcp_sent.Value() == expected_request.size(), "TCP SOME/IP send size changed");

  const auto tcp_received = tcp_peer.Value().Receive();
  Require(tcp_received.HasValue(), "TCP SOME/IP receive failed");
  Require(tcp_received.Value() == request, "TCP SOME/IP loopback message changed");

  const auto tcp_reply_sent = tcp_peer.Value().Send(request);
  Require(tcp_reply_sent.HasValue(), "TCP SOME/IP reply failed");
  const auto tcp_reply = tcp_client.Value().Receive();
  Require(tcp_reply.HasValue(), "TCP SOME/IP reply receive failed");
  Require(tcp_reply.Value() == request, "TCP SOME/IP reply message changed");

  const auto invalid_tcp_listen = TcpServer::Listen({.host = "not-an-ip-address", .port = 0U});
  Require(!invalid_tcp_listen.HasValue(), "invalid TCP listen address was accepted");

  return 0;
}
