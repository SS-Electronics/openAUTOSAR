// SPDX-License-Identifier: MIT

#include "openautosar/dds/rtps/endpoint_discovery.h"
#include "openautosar/dds/rtps/participant_discovery.h"
#include "openautosar/dds/rtps/rtps_message.h"
#include "openautosar/dds/rtps/udp_endpoint.h"

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
  namespace rtps = openautosar::dds::rtps;

  const rtps::RtpsMessage message{
    .vendor_id = {},
    .guid_prefix = {.value = {
      0x01U,
      0x02U,
      0x03U,
      0x04U,
      0x05U,
      0x06U,
      0x07U,
      0x08U,
      0x09U,
      0x0AU,
      0x0BU,
      0x0CU,
    }},
    .data = {{
      .reader_id = {.value = {0x00U, 0x00U, 0x04U, 0x07U}},
      .writer_id = {.value = {0x00U, 0x00U, 0x03U, 0x02U}},
      .writer_sequence_number = 1U,
      .serialized_payload = {0xAAU, 0xBBU, 0xCCU},
    }},
  };

  const std::vector<std::uint8_t> expected{
    0x52U, 0x54U, 0x50U, 0x53U, 0x02U, 0x03U, 0x4FU, 0x41U,
    0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
    0x09U, 0x0AU, 0x0BU, 0x0CU, 0x15U, 0x05U, 0x17U, 0x00U,
    0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x00U, 0x04U, 0x07U,
    0x00U, 0x00U, 0x03U, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x01U, 0x00U, 0x00U, 0x00U, 0xAAU, 0xBBU, 0xCCU,
  };

  const auto encoded = rtps::SerializeRtpsMessage(message);
  Require(encoded.HasValue(), "RTPS message did not serialize");
  Require(encoded.Value() == expected, "RTPS golden vector changed");

  const auto decoded = rtps::DeserializeRtpsMessage(expected);
  Require(decoded.HasValue(), "RTPS message did not deserialize");
  Require(decoded.Value() == message, "RTPS roundtrip changed");
  Require(
    rtps::ToString(rtps::kDataSubmessageKind) == std::string_view("data"),
    "RTPS text changed");
  Require(
    rtps::ToString(rtps::kHeartbeatSubmessageKind) == std::string_view("heartbeat"),
    "RTPS HEARTBEAT text changed");
  Require(
    rtps::ToString(rtps::kAckNackSubmessageKind) == std::string_view("acknack"),
    "RTPS ACKNACK text changed");

  const rtps::RtpsMessage heartbeat_message{
    .vendor_id = {},
    .guid_prefix = message.guid_prefix,
    .heartbeats = {{
      .reader_id = message.data[0U].reader_id,
      .writer_id = message.data[0U].writer_id,
      .first_sequence_number = 2U,
      .last_sequence_number = 4U,
      .count = 5U,
      .final_flag = true,
    }},
  };
  const std::vector<std::uint8_t> expected_heartbeat{
    0x52U, 0x54U, 0x50U, 0x53U, 0x02U, 0x03U, 0x4FU, 0x41U,
    0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
    0x09U, 0x0AU, 0x0BU, 0x0CU, 0x07U, 0x03U, 0x1CU, 0x00U,
    0x00U, 0x00U, 0x04U, 0x07U, 0x00U, 0x00U, 0x03U, 0x02U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U,
    0x05U, 0x00U, 0x00U, 0x00U,
  };
  const auto encoded_heartbeat = rtps::SerializeRtpsMessage(heartbeat_message);
  Require(encoded_heartbeat.HasValue(), "RTPS HEARTBEAT did not serialize");
  Require(encoded_heartbeat.Value() == expected_heartbeat, "RTPS HEARTBEAT vector changed");
  const auto decoded_heartbeat = rtps::DeserializeRtpsMessage(expected_heartbeat);
  Require(decoded_heartbeat.HasValue(), "RTPS HEARTBEAT did not deserialize");
  Require(decoded_heartbeat.Value() == heartbeat_message, "RTPS HEARTBEAT roundtrip changed");

  const rtps::RtpsMessage acknack_message{
    .vendor_id = {},
    .guid_prefix = message.guid_prefix,
    .acknacks = {{
      .reader_id = message.data[0U].reader_id,
      .writer_id = message.data[0U].writer_id,
      .bitmap_base = 2U,
      .missing_sequence_numbers = {2U, 4U},
      .count = 6U,
      .final_flag = true,
    }},
  };
  const std::vector<std::uint8_t> expected_acknack{
    0x52U, 0x54U, 0x50U, 0x53U, 0x02U, 0x03U, 0x4FU, 0x41U,
    0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
    0x09U, 0x0AU, 0x0BU, 0x0CU, 0x06U, 0x03U, 0x1CU, 0x00U,
    0x00U, 0x00U, 0x04U, 0x07U, 0x00U, 0x00U, 0x03U, 0x02U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U,
    0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xA0U,
    0x06U, 0x00U, 0x00U, 0x00U,
  };
  const auto encoded_acknack = rtps::SerializeRtpsMessage(acknack_message);
  Require(encoded_acknack.HasValue(), "RTPS ACKNACK did not serialize");
  Require(encoded_acknack.Value() == expected_acknack, "RTPS ACKNACK vector changed");
  const auto decoded_acknack = rtps::DeserializeRtpsMessage(expected_acknack);
  Require(decoded_acknack.HasValue(), "RTPS ACKNACK did not deserialize");
  Require(decoded_acknack.Value() == acknack_message, "RTPS ACKNACK roundtrip changed");

  auto invalid_heartbeat = heartbeat_message;
  invalid_heartbeat.heartbeats[0U].first_sequence_number = 5U;
  Require(
    !rtps::SerializeRtpsMessage(invalid_heartbeat).HasValue(),
    "invalid RTPS HEARTBEAT range was accepted");

  auto invalid_acknack = acknack_message;
  invalid_acknack.acknacks[0U].missing_sequence_numbers = {300U};
  Require(
    !rtps::SerializeRtpsMessage(invalid_acknack).HasValue(),
    "oversized RTPS ACKNACK bitmap was accepted");

  auto wrong_magic = expected;
  wrong_magic[0U] = 0x00U;
  Require(!rtps::DeserializeRtpsMessage(wrong_magic).HasValue(), "wrong RTPS magic was accepted");

  auto wrong_endian = expected;
  wrong_endian[21U] = rtps::kDataPayloadFlag;
  Require(
    !rtps::DeserializeRtpsMessage(wrong_endian).HasValue(),
    "big-endian RTPS submessage was accepted");

  auto unsupported_kind = expected;
  unsupported_kind[20U] = 0x16U;
  Require(
    !rtps::DeserializeRtpsMessage(unsupported_kind).HasValue(),
    "unsupported RTPS submessage was accepted");

  auto empty_payload = expected;
  empty_payload[22U] = 0x14U;
  empty_payload.resize(rtps::kRtpsHeaderSize + rtps::kDataSubmessageHeaderSize);
  Require(
    !rtps::DeserializeRtpsMessage(empty_payload).HasValue(),
    "empty RTPS DATA payload was accepted");

  rtps::RtpsMessage oversize = message;
  oversize.data[0U].serialized_payload.assign(rtps::kMaxUdpPayloadSize, 0xAAU);
  Require(
    !rtps::SerializeRtpsMessage(oversize).HasValue(),
    "oversized RTPS UDP payload was accepted");

  auto receiver = rtps::UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  auto sender = rtps::UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  Require(receiver.HasValue() && sender.HasValue(), "DDS/RTPS UDP endpoints did not bind");

  const auto receiver_address = receiver.Value().LocalAddress();
  Require(receiver_address.HasValue(), "DDS/RTPS receiver address unavailable");

  const auto sent = sender.Value().SendTo(message, receiver_address.Value());
  Require(sent.HasValue(), "DDS/RTPS UDP send failed");
  Require(sent.Value() == expected.size(), "DDS/RTPS UDP send size changed");

  const auto received = receiver.Value().Receive();
  Require(received.HasValue(), "DDS/RTPS UDP receive failed");
  Require(received.Value().message == message, "DDS/RTPS UDP message changed");
  Require(received.Value().remote.port != 0U, "DDS/RTPS remote endpoint port was not captured");

  const auto invalid_bind = rtps::UdpEndpoint::Bind({.host = "not-an-ip-address", .port = 0U});
  Require(!invalid_bind.HasValue(), "invalid DDS/RTPS UDP bind address was accepted");

  const rtps::SpdpParticipantData participant{
    .guid_prefix = message.guid_prefix,
    .vendor_id = {},
    .metatraffic_unicast_locator = {.host = "127.0.0.1", .port = 7410U},
    .default_unicast_locator = {.host = "127.0.0.1", .port = 7400U},
    .builtin_endpoint_set = rtps::kSpdpBuiltinParticipantEndpointSet,
    .lease_duration_ms = 3'000U,
  };

  const std::vector<std::uint8_t> expected_spdp{
    0x52U, 0x54U, 0x50U, 0x53U, 0x02U, 0x03U, 0x4FU, 0x41U,
    0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
    0x09U, 0x0AU, 0x0BU, 0x0CU, 0x15U, 0x05U, 0x8CU, 0x00U,
    0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x01U, 0x00U, 0xC7U,
    0x00U, 0x01U, 0x00U, 0xC2U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x03U, 0x00U, 0x00U,
    0x15U, 0x00U, 0x04U, 0x00U, 0x02U, 0x03U, 0x00U, 0x00U,
    0x16U, 0x00U, 0x04U, 0x00U, 0x4FU, 0x41U, 0x00U, 0x00U,
    0x50U, 0x00U, 0x10U, 0x00U, 0x01U, 0x02U, 0x03U, 0x04U,
    0x05U, 0x06U, 0x07U, 0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU,
    0x00U, 0x00U, 0x01U, 0xC1U, 0x32U, 0x00U, 0x18U, 0x00U,
    0x01U, 0x00U, 0x00U, 0x00U, 0xF2U, 0x1CU, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x7FU, 0x00U, 0x00U, 0x01U,
    0x31U, 0x00U, 0x18U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
    0xE8U, 0x1CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x7FU, 0x00U, 0x00U, 0x01U, 0x58U, 0x00U, 0x04U, 0x00U,
    0x03U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x08U, 0x00U,
    0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x01U, 0x00U, 0x00U, 0x00U,
  };

  const auto spdp = rtps::BuildSpdpParticipantAnnouncement(participant, 2U);
  Require(spdp.HasValue(), "SPDP participant announcement did not build");
  Require(spdp.Value().data[0U].writer_id == rtps::kSpdpBuiltinParticipantWriterId,
          "SPDP writer id changed");
  Require(spdp.Value().data[0U].reader_id == rtps::kSpdpBuiltinParticipantReaderId,
          "SPDP reader id changed");

  const auto encoded_spdp = rtps::SerializeRtpsMessage(spdp.Value());
  Require(encoded_spdp.HasValue(), "SPDP participant announcement did not serialize");
  Require(encoded_spdp.Value() == expected_spdp, "SPDP participant golden vector changed");

  const auto decoded_spdp = rtps::DeserializeRtpsMessage(expected_spdp);
  Require(decoded_spdp.HasValue(), "SPDP participant announcement did not deserialize");
  const auto extracted_spdp = rtps::ExtractSpdpParticipantAnnouncement(decoded_spdp.Value());
  Require(extracted_spdp.HasValue(), "SPDP participant data did not extract");
  Require(extracted_spdp.Value() == participant, "SPDP participant roundtrip changed");

  auto wrong_spdp_writer = spdp.Value();
  wrong_spdp_writer.data[0U].writer_id = {.value = {0x00U, 0x01U, 0x00U, 0xC3U}};
  Require(
    !rtps::ExtractSpdpParticipantAnnouncement(wrong_spdp_writer).HasValue(),
    "wrong SPDP writer id was accepted");

  auto truncated_spdp = spdp.Value();
  truncated_spdp.data[0U].serialized_payload.resize(3U);
  Require(
    !rtps::ExtractSpdpParticipantAnnouncement(truncated_spdp).HasValue(),
    "truncated SPDP payload was accepted");

  auto invalid_participant = participant;
  invalid_participant.default_unicast_locator.host = "not-an-ip-address";
  Require(
    !rtps::BuildSpdpParticipantAnnouncement(invalid_participant, 3U).HasValue(),
    "invalid SPDP locator address was accepted");

  const rtps::SedpEndpointData publication{
    .participant_guid_prefix = message.guid_prefix,
    .endpoint_id = {.value = {0x00U, 0x00U, 0x03U, 0x02U}},
    .topic_name = "openautosar.ultrasonic.DistanceSample",
    .type_name = "openautosar.virtual_vehicle.DistanceSample",
    .unicast_locator = {.host = "127.0.0.1", .port = 7400U},
    .reliability_kind = rtps::kSedpReliabilityBestEffort,
    .durability_kind = rtps::kSedpDurabilityVolatile,
  };

  const std::vector<std::uint8_t> expected_sedp_publication{
    0x52U, 0x54U, 0x50U, 0x53U, 0x02U, 0x03U, 0x4FU, 0x41U,
    0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
    0x09U, 0x0AU, 0x0BU, 0x0CU, 0x15U, 0x05U, 0xC0U, 0x00U,
    0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x00U, 0x03U, 0xC7U,
    0x00U, 0x00U, 0x03U, 0xC2U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x03U, 0x00U, 0x00U,
    0x5AU, 0x00U, 0x10U, 0x00U, 0x01U, 0x02U, 0x03U, 0x04U,
    0x05U, 0x06U, 0x07U, 0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU,
    0x00U, 0x00U, 0x03U, 0x02U, 0x05U, 0x00U, 0x2AU, 0x00U,
    0x26U, 0x00U, 0x00U, 0x00U, 0x6FU, 0x70U, 0x65U, 0x6EU,
    0x61U, 0x75U, 0x74U, 0x6FU, 0x73U, 0x61U, 0x72U, 0x2EU,
    0x75U, 0x6CU, 0x74U, 0x72U, 0x61U, 0x73U, 0x6FU, 0x6EU,
    0x69U, 0x63U, 0x2EU, 0x44U, 0x69U, 0x73U, 0x74U, 0x61U,
    0x6EU, 0x63U, 0x65U, 0x53U, 0x61U, 0x6DU, 0x70U, 0x6CU,
    0x65U, 0x00U, 0x00U, 0x00U, 0x07U, 0x00U, 0x2FU, 0x00U,
    0x2BU, 0x00U, 0x00U, 0x00U, 0x6FU, 0x70U, 0x65U, 0x6EU,
    0x61U, 0x75U, 0x74U, 0x6FU, 0x73U, 0x61U, 0x72U, 0x2EU,
    0x76U, 0x69U, 0x72U, 0x74U, 0x75U, 0x61U, 0x6CU, 0x5FU,
    0x76U, 0x65U, 0x68U, 0x69U, 0x63U, 0x6CU, 0x65U, 0x2EU,
    0x44U, 0x69U, 0x73U, 0x74U, 0x61U, 0x6EU, 0x63U, 0x65U,
    0x53U, 0x61U, 0x6DU, 0x70U, 0x6CU, 0x65U, 0x00U, 0x00U,
    0x2FU, 0x00U, 0x18U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
    0xE8U, 0x1CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x7FU, 0x00U, 0x00U, 0x01U, 0x1AU, 0x00U, 0x04U, 0x00U,
    0x01U, 0x00U, 0x00U, 0x00U, 0x1DU, 0x00U, 0x04U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
  };

  const auto sedp_publication = rtps::BuildSedpEndpointAnnouncement(
    publication,
    rtps::SedpEndpointKind::kPublication,
    3U);
  Require(sedp_publication.HasValue(), "SEDP publication announcement did not build");
  Require(
    sedp_publication.Value().data[0U].writer_id == rtps::kSedpBuiltinPublicationsWriterId,
    "SEDP publication writer id changed");
  Require(
    sedp_publication.Value().data[0U].reader_id == rtps::kSedpBuiltinPublicationsReaderId,
    "SEDP publication reader id changed");

  const auto encoded_sedp = rtps::SerializeRtpsMessage(sedp_publication.Value());
  Require(encoded_sedp.HasValue(), "SEDP publication announcement did not serialize");
  Require(
    encoded_sedp.Value() == expected_sedp_publication,
    "SEDP publication golden vector changed");
  const auto decoded_sedp = rtps::DeserializeRtpsMessage(encoded_sedp.Value());
  Require(decoded_sedp.HasValue(), "SEDP publication announcement did not deserialize");
  const auto extracted_sedp = rtps::ExtractSedpEndpointAnnouncement(
    decoded_sedp.Value(),
    rtps::SedpEndpointKind::kPublication);
  Require(extracted_sedp.HasValue(), "SEDP publication data did not extract");
  Require(extracted_sedp.Value() == publication, "SEDP publication roundtrip changed");

  Require(
    !rtps::ExtractSedpEndpointAnnouncement(
       decoded_sedp.Value(),
       rtps::SedpEndpointKind::kSubscription)
       .HasValue(),
    "SEDP publication was accepted as a subscription announcement");

  auto invalid_publication = publication;
  invalid_publication.topic_name.clear();
  Require(
    !rtps::BuildSedpEndpointAnnouncement(
       invalid_publication,
       rtps::SedpEndpointKind::kPublication,
       4U)
       .HasValue(),
    "invalid SEDP endpoint name was accepted");

  invalid_publication = publication;
  invalid_publication.reliability_kind = 99U;
  Require(
    !rtps::BuildSedpEndpointAnnouncement(
       invalid_publication,
       rtps::SedpEndpointKind::kPublication,
       5U)
       .HasValue(),
    "unsupported SEDP reliability policy was accepted");

  return 0;
}
