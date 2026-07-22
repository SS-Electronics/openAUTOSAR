// SPDX-License-Identifier: MIT

#include "openautosar/com/service_registry.h"
#include "openautosar/dds/rtps/rtps_message.h"
#include "openautosar/dds/rtps/udp_endpoint.h"
#include "openautosar/dds_binding/dds_binding.h"

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
  namespace com = openautosar::com;
  namespace rtps = openautosar::dds::rtps;
  using openautosar::dds_binding::DdsBestEffortReader;
  using openautosar::dds_binding::DdsBestEffortWriter;
  using openautosar::dds_binding::DdsBinding;
  using openautosar::dds_binding::DdsDiscoveryCache;
  using openautosar::dds_binding::DdsDurabilityPolicy;
  using openautosar::dds_binding::DdsEndpointMatch;
  using openautosar::dds_binding::DdsEndpointRole;
  using openautosar::dds_binding::DdsLivelinessPolicy;
  using openautosar::dds_binding::DdsOwnershipPolicy;
  using openautosar::dds_binding::DdsReliabilityPolicy;
  using openautosar::dds_binding::DdsReliableReader;
  using openautosar::dds_binding::DdsReliableWriter;
  using openautosar::dds_binding::DdsTopicMapping;

  constexpr com::ServiceIdentifier ultrasonic_service{
    .interface_id = 0x0A500001U,
    .instance_id = 0x00000001U,
    .major_version = 1U,
    .minor_version = 0U,
  };

  const DdsTopicMapping mapping{
    .ara_service = ultrasonic_service,
    .topic_name = "openautosar.ultrasonic.DistanceSample",
    .type_name = "openautosar.virtual_vehicle.DistanceSample",
    .event_name = "DistanceSample",
    .participant_guid_prefix = {.value = {
      0x0AU,
      0x50U,
      0x00U,
      0x01U,
      0x00U,
      0x00U,
      0x00U,
      0x01U,
      0x00U,
      0x00U,
      0x00U,
      0x01U,
    }},
  };

  com::ServiceRegistry registry;
  const auto offer = registry.OfferService({
    .service = ultrasonic_service,
    .process_identity = "dds-binding-provider",
    .machine_identity = "qemux86-64",
    .endpoint = {.binding = com::Binding::kDds, .address = "127.0.0.1", .port = 7400U},
    .ttl_ms = 1'000U,
    .access_policy = "unit-test",
    .deployment_provenance = "dds-binding-test",
  });
  Require(offer.HasValue(), "DDS service offer failed");

  const auto subscription = registry.Subscribe(ultrasonic_service, "DistanceSample", 2U);
  Require(subscription.HasValue(), "DDS registry subscription failed");

  const com::EventSample sample{
    .service = ultrasonic_service,
    .event_name = "DistanceSample",
    .payload = {0x01U, 0x2CU, 0x62U, 0x10U},
  };

  const auto message = DdsBinding::BuildDataMessage(mapping, sample, 7U);
  Require(message.HasValue(), "DDS binding DATA message build failed");
  Require(
    message.Value().guid_prefix == mapping.participant_guid_prefix,
    "DDS GUID prefix changed");
  Require(message.Value().data.size() == 1U, "DDS DATA submessage count changed");
  Require(message.Value().data[0U].writer_id == mapping.writer_id, "DDS writer id changed");
  Require(message.Value().data[0U].reader_id == mapping.reader_id, "DDS reader id changed");
  Require(message.Value().data[0U].writer_sequence_number == 7U, "DDS sequence number changed");

  const auto encoded = rtps::SerializeRtpsMessage(message.Value());
  Require(encoded.HasValue(), "DDS binding RTPS serialization failed");

  const auto decoded_sample = DdsBinding::DecodeDataMessage(mapping, message.Value());
  Require(decoded_sample.HasValue(), "DDS binding DATA decode failed");
  Require(decoded_sample.Value().service == ultrasonic_service, "decoded DDS service changed");
  Require(decoded_sample.Value().event_name == "DistanceSample", "decoded DDS event name changed");
  Require(decoded_sample.Value().payload == sample.payload, "decoded DDS payload changed");

  const auto published_direct = registry.Publish(decoded_sample.Value());
  Require(published_direct.HasValue(), "decoded DDS sample did not publish to registry");
  const auto delivered_direct = registry.Poll(subscription.Value().id);
  Require(delivered_direct.HasValue(), "registry did not deliver decoded DDS sample");
  Require(
    delivered_direct.Value().payload == sample.payload,
    "registry DDS direct payload changed");

  DdsTopicMapping wrong_topic = mapping;
  wrong_topic.topic_name = "openautosar.ultrasonic.OtherSample";
  Require(
    !DdsBinding::DecodeDataMessage(wrong_topic, message.Value()).HasValue(),
    "wrong DDS topic mapping was accepted");

  auto wrong_writer = message.Value();
  wrong_writer.data[0U].writer_id = {.value = {0x00U, 0x00U, 0x03U, 0x03U}};
  Require(
    !DdsBinding::DecodeDataMessage(mapping, wrong_writer).HasValue(),
    "wrong DDS writer id was accepted");

  const auto zero_sequence = DdsBinding::BuildDataMessage(mapping, sample, 0U);
  Require(!zero_sequence.HasValue(), "zero DDS sequence number was accepted");

  const auto wrong_sample = DdsBinding::BuildDataMessage(
    mapping,
    {
      .service = ultrasonic_service,
      .event_name = "OtherEvent",
      .payload = {0x01U},
    },
    8U);
  Require(!wrong_sample.HasValue(), "wrong ara::com event name was accepted by DDS binding");

  auto receiver = rtps::UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  auto sender = rtps::UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  Require(receiver.HasValue() && sender.HasValue(), "DDS binding UDP endpoints did not bind");

  const auto receiver_address = receiver.Value().LocalAddress();
  Require(receiver_address.HasValue(), "DDS binding receiver address unavailable");

  const auto sent = DdsBinding::PublishEvent(
    sender.Value(),
    receiver_address.Value(),
    mapping,
    sample,
    9U);
  Require(sent.HasValue(), "DDS binding UDP publish failed");

  const auto received_sample = DdsBinding::ReceiveEvent(receiver.Value(), mapping);
  Require(received_sample.HasValue(), "DDS binding UDP receive failed");
  Require(received_sample.Value().payload == sample.payload, "DDS UDP received payload changed");

  const auto published_udp = registry.Publish(received_sample.Value());
  Require(published_udp.HasValue(), "received DDS sample did not publish to registry");
  const auto delivered_udp = registry.Poll(subscription.Value().id);
  Require(delivered_udp.HasValue(), "registry did not deliver received DDS sample");
  Require(delivered_udp.Value().sequence == published_udp.Value(), "DDS registry sequence changed");

  const auto unsupported = DdsBinding::UnsupportedFeatureMatrix();
  Require(!unsupported.empty(), "DDS unsupported-feature matrix is empty");
  Require(
    unsupported[0U].feature == "DDS discovery lifecycle",
    "DDS unsupported-feature matrix first item changed");

  const auto publication_discovery = DdsBinding::BuildEndpointDiscovery(
    mapping,
    DdsEndpointRole::kPublication,
    {.host = "127.0.0.1", .port = 7400U},
    10U);
  Require(publication_discovery.HasValue(), "DDS publication discovery did not build");

  const auto publication_endpoint = rtps::ExtractSedpEndpointAnnouncement(
    publication_discovery.Value(),
    rtps::SedpEndpointKind::kPublication);
  Require(publication_endpoint.HasValue(), "DDS publication discovery did not extract");
  Require(publication_endpoint.Value().endpoint_id == mapping.writer_id,
          "DDS publication discovery writer id changed");
  Require(publication_endpoint.Value().topic_name == mapping.topic_name,
          "DDS publication discovery topic changed");
  Require(
    publication_endpoint.Value().reliability_kind == rtps::kSedpReliabilityBestEffort,
    "DDS publication discovery default reliability QoS changed");
  Require(
    publication_endpoint.Value().durability_kind == rtps::kSedpDurabilityVolatile,
    "DDS publication discovery default durability QoS changed");

  const auto subscription_discovery = DdsBinding::BuildEndpointDiscovery(
    mapping,
    DdsEndpointRole::kSubscription,
    {.host = "127.0.0.1", .port = 7400U},
    11U);
  Require(subscription_discovery.HasValue(), "DDS subscription discovery did not build");

  const auto subscription_endpoint = rtps::ExtractSedpEndpointAnnouncement(
    subscription_discovery.Value(),
    rtps::SedpEndpointKind::kSubscription);
  Require(subscription_endpoint.HasValue(), "DDS subscription discovery did not extract");
  Require(subscription_endpoint.Value().endpoint_id == mapping.reader_id,
          "DDS subscription discovery reader id changed");

  DdsTopicMapping zero_history_mapping = mapping;
  zero_history_mapping.qos.history_depth = 0U;
  Require(
    !DdsBinding::BuildEndpointDiscovery(
       zero_history_mapping,
       DdsEndpointRole::kPublication,
       {.host = "127.0.0.1", .port = 7400U},
       12U)
       .HasValue(),
    "DDS zero history QoS depth was accepted");

  DdsTopicMapping undersized_cache_mapping = mapping;
  undersized_cache_mapping.qos.history_depth = 4U;
  undersized_cache_mapping.qos.max_cached_samples = 3U;
  Require(
    !DdsBinding::BuildEndpointDiscovery(
       undersized_cache_mapping,
       DdsEndpointRole::kPublication,
       {.host = "127.0.0.1", .port = 7400U},
       12U)
       .HasValue(),
    "DDS cached-sample resource limit below history depth was accepted");

  DdsTopicMapping manual_liveliness_mapping = mapping;
  manual_liveliness_mapping.qos.liveliness = DdsLivelinessPolicy::kManualByParticipant;
  Require(
    !DdsBinding::BuildEndpointDiscovery(
       manual_liveliness_mapping,
       DdsEndpointRole::kPublication,
       {.host = "127.0.0.1", .port = 7400U},
       12U)
       .HasValue(),
    "DDS unsupported manual liveliness QoS was accepted");

  DdsTopicMapping exclusive_ownership_mapping = mapping;
  exclusive_ownership_mapping.qos.ownership = DdsOwnershipPolicy::kExclusive;
  Require(
    !DdsBinding::BuildEndpointDiscovery(
       exclusive_ownership_mapping,
       DdsEndpointRole::kPublication,
       {.host = "127.0.0.1", .port = 7400U},
       12U)
       .HasValue(),
    "DDS unsupported exclusive ownership QoS was accepted");

  DdsTopicMapping remote_mapping = mapping;
  remote_mapping.participant_guid_prefix = {.value = {
    0x0AU,
    0x50U,
    0x00U,
    0x01U,
    0x00U,
    0x00U,
    0x00U,
    0x02U,
    0x00U,
    0x00U,
    0x00U,
    0x02U,
  }};

  DdsTopicMapping reliable_mapping = mapping;
  reliable_mapping.qos.reliability = DdsReliabilityPolicy::kReliable;
  reliable_mapping.qos.history_depth = 4U;
  DdsTopicMapping remote_reliable_mapping = remote_mapping;
  remote_reliable_mapping.qos = reliable_mapping.qos;

  DdsDiscoveryCache discovery_cache;
  const auto remote_publication_discovery = DdsBinding::BuildEndpointDiscovery(
    remote_mapping,
    DdsEndpointRole::kPublication,
    {.host = "10.0.0.20", .port = 7410U},
    12U);
  Require(
    remote_publication_discovery.HasValue(),
    "DDS remote publication discovery did not build");

  const auto observed_publication = discovery_cache.ObserveEndpointDiscovery(
    DdsEndpointRole::kPublication,
    remote_publication_discovery.Value(),
    1'000U);
  Require(observed_publication.HasValue(), "DDS remote publication was not observed");
  Require(
    observed_publication.Value().last_sequence_number == 12U,
    "DDS observed publication sequence changed");

  const auto subscription_matches = discovery_cache.MatchesFor(
    mapping,
    DdsEndpointRole::kSubscription,
    1'100U,
    500U);
  Require(subscription_matches.HasValue(), "DDS subscription match lookup failed");
  Require(subscription_matches.Value().size() == 1U, "DDS subscription match count changed");
  Require(
    subscription_matches.Value()[0U].local_endpoint_id == mapping.reader_id,
    "DDS subscription local endpoint id changed");
  Require(
    subscription_matches.Value()[0U].remote_endpoint.endpoint_id == remote_mapping.writer_id,
    "DDS matched remote writer id changed");
  Require(
    subscription_matches.Value()[0U].remote_endpoint.unicast_locator.port == 7410U,
    "DDS matched remote locator port changed");

  const auto publication_matches_before_reader = discovery_cache.MatchesFor(
    mapping,
    DdsEndpointRole::kPublication,
    1'100U,
    500U);
  Require(
    publication_matches_before_reader.HasValue(),
    "DDS publication match lookup without reader failed");
  Require(
    publication_matches_before_reader.Value().empty(),
    "DDS publication matched a remote publication");

  const auto replayed_publication = discovery_cache.ObserveEndpointDiscovery(
    DdsEndpointRole::kPublication,
    remote_publication_discovery.Value(),
    1'150U);
  Require(!replayed_publication.HasValue(), "DDS replayed endpoint announcement was accepted");

  const auto refreshed_publication_discovery = DdsBinding::BuildEndpointDiscovery(
    remote_mapping,
    DdsEndpointRole::kPublication,
    {.host = "10.0.0.21", .port = 7411U},
    13U);
  Require(
    refreshed_publication_discovery.HasValue(),
    "DDS refreshed publication discovery did not build");
  const auto refreshed_publication = discovery_cache.ObserveEndpointDiscovery(
    DdsEndpointRole::kPublication,
    refreshed_publication_discovery.Value(),
    1'200U);
  Require(refreshed_publication.HasValue(), "DDS refreshed publication was not observed");

  const auto refreshed_subscription_matches = discovery_cache.MatchesFor(
    mapping,
    DdsEndpointRole::kSubscription,
    1'250U,
    500U);
  Require(
    refreshed_subscription_matches.HasValue(),
    "DDS refreshed subscription match lookup failed");
  Require(
    refreshed_subscription_matches.Value().size() == 1U,
    "DDS refreshed subscription match count changed");
  Require(
    refreshed_subscription_matches.Value()[0U].remote_endpoint.unicast_locator.port == 7411U,
    "DDS refreshed remote locator did not update");

  const auto remote_subscription_discovery = DdsBinding::BuildEndpointDiscovery(
    remote_mapping,
    DdsEndpointRole::kSubscription,
    {.host = "10.0.0.30", .port = 7420U},
    14U);
  Require(
    remote_subscription_discovery.HasValue(),
    "DDS remote subscription discovery did not build");
  const auto observed_subscription = discovery_cache.ObserveEndpointDiscovery(
    DdsEndpointRole::kSubscription,
    remote_subscription_discovery.Value(),
    1'210U);
  Require(observed_subscription.HasValue(), "DDS remote subscription was not observed");

  const auto publication_matches = discovery_cache.MatchesFor(
    mapping,
    DdsEndpointRole::kPublication,
    1'250U,
    500U);
  Require(publication_matches.HasValue(), "DDS publication match lookup failed");
  Require(publication_matches.Value().size() == 1U, "DDS publication match count changed");
  Require(
    publication_matches.Value()[0U].local_endpoint_id == mapping.writer_id,
    "DDS publication local endpoint id changed");
  Require(
    publication_matches.Value()[0U].remote_endpoint.endpoint_id == remote_mapping.reader_id,
    "DDS matched remote reader id changed");

  DdsTopicMapping other_topic_mapping = remote_mapping;
  other_topic_mapping.participant_guid_prefix.value[11U] = 0x03U;
  other_topic_mapping.topic_name = "openautosar.ultrasonic.OtherSample";
  const auto other_topic_discovery = DdsBinding::BuildEndpointDiscovery(
    other_topic_mapping,
    DdsEndpointRole::kPublication,
    {.host = "10.0.0.40", .port = 7430U},
    15U);
  Require(other_topic_discovery.HasValue(), "DDS other-topic discovery did not build");
  const auto observed_other_topic = discovery_cache.ObserveEndpointDiscovery(
    DdsEndpointRole::kPublication,
    other_topic_discovery.Value(),
    1'220U);
  Require(
    observed_other_topic.HasValue(),
    "DDS other-topic endpoint was not observed");

  DdsTopicMapping qos_mismatch_mapping = remote_reliable_mapping;
  qos_mismatch_mapping.participant_guid_prefix = {.value = {
    0x0AU,
    0x50U,
    0x00U,
    0x01U,
    0x00U,
    0x00U,
    0x00U,
    0x04U,
    0x00U,
    0x00U,
    0x00U,
    0x04U,
  }};
  const auto reliable_discovery = DdsBinding::BuildEndpointDiscovery(
    qos_mismatch_mapping,
    DdsEndpointRole::kPublication,
    {.host = "10.0.0.50", .port = 7440U},
    16U);
  Require(reliable_discovery.HasValue(), "DDS reliable endpoint discovery did not build");
  const auto reliable_endpoint = rtps::ExtractSedpEndpointAnnouncement(
    reliable_discovery.Value(),
    rtps::SedpEndpointKind::kPublication);
  Require(reliable_endpoint.HasValue(), "DDS reliable endpoint discovery did not extract");
  Require(
    reliable_endpoint.Value().reliability_kind == rtps::kSedpReliabilityReliable,
    "DDS reliable endpoint discovery reliability QoS changed");
  Require(
    reliable_endpoint.Value().durability_kind == rtps::kSedpDurabilityVolatile,
    "DDS reliable endpoint discovery durability QoS changed");
  Require(
    discovery_cache
      .ObserveEndpointDiscovery(DdsEndpointRole::kPublication, reliable_discovery.Value(), 1'230U)
      .HasValue(),
    "DDS reliable endpoint was not observed");

  const auto self_discovery = DdsBinding::BuildEndpointDiscovery(
    mapping,
    DdsEndpointRole::kPublication,
    {.host = "127.0.0.1", .port = 7400U},
    17U);
  Require(self_discovery.HasValue(), "DDS self discovery did not build");
  Require(
    discovery_cache
      .ObserveEndpointDiscovery(DdsEndpointRole::kPublication, self_discovery.Value(), 1'240U)
      .HasValue(),
    "DDS self endpoint was not observed");

  const auto filtered_subscription_matches = discovery_cache.MatchesFor(
    mapping,
    DdsEndpointRole::kSubscription,
    1'260U,
    500U);
  Require(
    filtered_subscription_matches.HasValue(),
    "DDS filtered subscription match lookup failed");
  Require(
    filtered_subscription_matches.Value().size() == 1U,
    "DDS topic/QoS/self filters changed subscription match count");

  const auto reliable_subscription_matches = discovery_cache.MatchesFor(
    reliable_mapping,
    DdsEndpointRole::kSubscription,
    1'260U,
    500U);
  Require(
    reliable_subscription_matches.HasValue(),
    "DDS reliable subscription match lookup failed");
  Require(
    reliable_subscription_matches.Value().size() == 1U,
    "DDS reliable subscription match count changed");
  Require(
    reliable_subscription_matches.Value()[0U].remote_endpoint.reliability_kind ==
      rtps::kSedpReliabilityReliable,
    "DDS reliable subscription matched a non-reliable endpoint");

  DdsTopicMapping transient_mapping = reliable_mapping;
  transient_mapping.qos.durability = DdsDurabilityPolicy::kTransientLocal;
  const auto transient_discovery = DdsBinding::BuildEndpointDiscovery(
    transient_mapping,
    DdsEndpointRole::kPublication,
    {.host = "127.0.0.1", .port = 7450U},
    18U);
  Require(transient_discovery.HasValue(), "DDS transient-local discovery did not build");
  const auto transient_endpoint = rtps::ExtractSedpEndpointAnnouncement(
    transient_discovery.Value(),
    rtps::SedpEndpointKind::kPublication);
  Require(transient_endpoint.HasValue(), "DDS transient-local discovery did not extract");
  Require(
    transient_endpoint.Value().durability_kind == rtps::kSedpDurabilityTransientLocal,
    "DDS transient-local durability QoS did not map to SEDP");

  DdsBestEffortWriter best_effort_writer(mapping);
  const auto matched_publication_sample = best_effort_writer.BuildSample(
    publication_matches.Value()[0U],
    sample);
  Require(matched_publication_sample.HasValue(), "DDS matched publication DATA did not build");
  Require(
    matched_publication_sample.Value().guid_prefix == mapping.participant_guid_prefix,
    "DDS best-effort DATA GUID prefix changed");
  Require(
    matched_publication_sample.Value().data[0U].writer_id == mapping.writer_id,
    "DDS best-effort DATA writer id changed");
  Require(
    matched_publication_sample.Value().data[0U].reader_id == remote_mapping.reader_id,
    "DDS best-effort DATA reader id did not use matched remote reader");
  Require(
    matched_publication_sample.Value().data[0U].writer_sequence_number == 1U,
    "DDS best-effort DATA first sequence number changed");
  Require(
    best_effort_writer.Snapshot().next_sequence_number == 2U,
    "DDS best-effort writer next sequence did not advance");
  Require(
    !best_effort_writer.BuildSample(refreshed_subscription_matches.Value()[0U], sample)
       .HasValue(),
    "DDS best-effort writer accepted a subscription-side match");
  Require(
    best_effort_writer.Snapshot().samples_sent == 1U,
    "DDS failed best-effort writer match changed send count");

  DdsDiscoveryCache remote_discovery_cache;
  const auto remote_cache_subscription = remote_discovery_cache.ObserveEndpointDiscovery(
    DdsEndpointRole::kSubscription,
    subscription_discovery.Value(),
    1'270U);
  Require(
    remote_cache_subscription.HasValue(),
    "DDS remote cache did not observe local subscription");
  const auto remote_cache_publication = remote_discovery_cache.ObserveEndpointDiscovery(
    DdsEndpointRole::kPublication,
    publication_discovery.Value(),
    1'271U);
  Require(
    remote_cache_publication.HasValue(),
    "DDS remote cache did not observe local publication");
  const auto remote_publication_matches = remote_discovery_cache.MatchesFor(
    remote_mapping,
    DdsEndpointRole::kPublication,
    1'280U,
    500U);
  Require(
    remote_publication_matches.HasValue(),
    "DDS remote publication best-effort match lookup failed");
  Require(
    remote_publication_matches.Value().size() == 1U,
    "DDS remote publication best-effort match count changed");

  DdsBestEffortWriter remote_best_effort_writer(remote_mapping);
  const auto remote_writer_message = remote_best_effort_writer.BuildSample(
    remote_publication_matches.Value()[0U],
    sample);
  Require(remote_writer_message.HasValue(), "DDS remote best-effort DATA did not build");

  DdsBestEffortReader best_effort_reader(mapping);
  const auto accepted_sample = best_effort_reader.AcceptSample(
    refreshed_subscription_matches.Value()[0U],
    remote_writer_message.Value(),
    1'290U);
  Require(accepted_sample.HasValue(), "DDS best-effort reader rejected matched DATA");
  Require(accepted_sample.Value().payload == sample.payload, "DDS best-effort payload changed");
  Require(accepted_sample.Value().sequence == 1U, "DDS best-effort sample sequence changed");
  const auto duplicate_sample = best_effort_reader.AcceptSample(
    refreshed_subscription_matches.Value()[0U],
    remote_writer_message.Value(),
    1'291U);
  Require(
    !duplicate_sample.HasValue(),
    "DDS best-effort reader accepted duplicate DATA");
  const auto wrong_role_sample = best_effort_reader.AcceptSample(
    publication_matches.Value()[0U],
    remote_writer_message.Value(),
    1'292U);
  Require(
    !wrong_role_sample.HasValue(),
    "DDS best-effort reader accepted a publication-side match");

  const auto second_remote_writer_message = remote_best_effort_writer.BuildSample(
    remote_publication_matches.Value()[0U],
    sample);
  Require(
    second_remote_writer_message.HasValue(),
    "DDS second remote best-effort DATA did not build");
  const auto second_accepted_sample = best_effort_reader.AcceptSample(
    refreshed_subscription_matches.Value()[0U],
    second_remote_writer_message.Value(),
    1'300U);
  Require(
    second_accepted_sample.HasValue(),
    "DDS best-effort reader rejected newer DATA");
  const auto reader_snapshot = best_effort_reader.Snapshot();
  Require(reader_snapshot.samples_received == 2U, "DDS best-effort receive count changed");
  Require(
    reader_snapshot.stale_samples_rejected == 1U,
    "DDS best-effort stale rejection count changed");
  Require(
    reader_snapshot.writer_states.size() == 1U,
    "DDS best-effort writer state count changed");
  Require(
    reader_snapshot.writer_states[0U].last_sequence_number == 2U,
    "DDS best-effort writer state sequence changed");

  DdsTopicMapping monitored_mapping = mapping;
  monitored_mapping.qos.deadline_ms = 5U;
  monitored_mapping.qos.liveliness_lease_duration_ms = 10U;
  monitored_mapping.qos.max_remote_writers = 1U;
  DdsBestEffortReader monitored_best_effort_reader(monitored_mapping);
  Require(
    monitored_best_effort_reader
      .AcceptSample(
        refreshed_subscription_matches.Value()[0U],
        remote_writer_message.Value(),
        2'000U)
      .HasValue(),
    "DDS monitored best-effort reader rejected first DATA");
  Require(
    monitored_best_effort_reader
      .AcceptSample(
        refreshed_subscription_matches.Value()[0U],
        second_remote_writer_message.Value(),
        2'010U)
      .HasValue(),
    "DDS monitored best-effort reader rejected deadline-late DATA");
  auto monitored_snapshot = monitored_best_effort_reader.Snapshot();
  Require(
    monitored_snapshot.deadline_misses == 1U,
    "DDS best-effort deadline miss count changed");
  const auto removed_best_effort_writers =
    monitored_best_effort_reader.RemoveExpiredWriters(2'025U);
  Require(
    removed_best_effort_writers.HasValue() && removed_best_effort_writers.Value() == 1U,
    "DDS best-effort liveliness removal count changed");
  monitored_snapshot = monitored_best_effort_reader.Snapshot();
  Require(
    monitored_snapshot.liveliness_lost == 1U && monitored_snapshot.writer_states.empty(),
    "DDS best-effort liveliness state changed");

  DdsTopicMapping second_remote_mapping = remote_mapping;
  second_remote_mapping.participant_guid_prefix.value[11U] = 0x05U;
  const DdsEndpointMatch second_remote_writer_match{
    .local_role = DdsEndpointRole::kSubscription,
    .local_endpoint_id = monitored_mapping.reader_id,
    .remote_endpoint = {
      .role = DdsEndpointRole::kPublication,
      .participant_guid_prefix = second_remote_mapping.participant_guid_prefix,
      .endpoint_id = second_remote_mapping.writer_id,
      .topic_name = second_remote_mapping.topic_name,
      .type_name = second_remote_mapping.type_name,
      .unicast_locator = {.host = "10.0.0.70", .port = 7470U},
      .reliability_kind = rtps::kSedpReliabilityBestEffort,
      .durability_kind = rtps::kSedpDurabilityVolatile,
    },
  };
  const auto second_remote_writer_message_direct = DdsBinding::BuildDataMessage(
    second_remote_mapping,
    sample,
    1U);
  Require(
    second_remote_writer_message_direct.HasValue(),
    "DDS second best-effort remote DATA did not build");
  DdsBestEffortReader resource_limited_best_effort_reader(monitored_mapping);
  Require(
    resource_limited_best_effort_reader
      .AcceptSample(
        refreshed_subscription_matches.Value()[0U],
        remote_writer_message.Value(),
        3'000U)
      .HasValue(),
    "DDS resource-limited best-effort reader rejected first writer");
  Require(
    !resource_limited_best_effort_reader
       .AcceptSample(
         second_remote_writer_match,
         second_remote_writer_message_direct.Value(),
         3'001U)
       .HasValue(),
    "DDS best-effort reader accepted a writer above the resource limit");
  Require(
    resource_limited_best_effort_reader.Snapshot().resource_limit_rejections == 1U,
    "DDS best-effort resource-limit rejection count changed");

  auto best_effort_receiver = rtps::UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  auto best_effort_sender = rtps::UdpEndpoint::Bind({.host = "127.0.0.1", .port = 0U});
  Require(
    best_effort_receiver.HasValue() && best_effort_sender.HasValue(),
    "DDS best-effort UDP endpoints did not bind");
  const auto best_effort_receiver_address = best_effort_receiver.Value().LocalAddress();
  const auto best_effort_sender_address = best_effort_sender.Value().LocalAddress();
  Require(
    best_effort_receiver_address.HasValue() && best_effort_sender_address.HasValue(),
    "DDS best-effort UDP endpoint addresses unavailable");

  const auto dynamic_remote_subscription = DdsBinding::BuildEndpointDiscovery(
    remote_mapping,
    DdsEndpointRole::kSubscription,
    best_effort_receiver_address.Value(),
    18U);
  const auto dynamic_local_publication = DdsBinding::BuildEndpointDiscovery(
    mapping,
    DdsEndpointRole::kPublication,
    best_effort_sender_address.Value(),
    18U);
  Require(
    dynamic_remote_subscription.HasValue() && dynamic_local_publication.HasValue(),
    "DDS dynamic best-effort discovery did not build");

  DdsDiscoveryCache local_udp_cache;
  DdsDiscoveryCache remote_udp_cache;
  const auto local_udp_observed = local_udp_cache.ObserveEndpointDiscovery(
    DdsEndpointRole::kSubscription,
    dynamic_remote_subscription.Value(),
    1'310U);
  Require(
    local_udp_observed.HasValue(),
    "DDS local UDP cache did not observe remote subscription");
  const auto remote_udp_observed = remote_udp_cache.ObserveEndpointDiscovery(
    DdsEndpointRole::kPublication,
    dynamic_local_publication.Value(),
    1'310U);
  Require(
    remote_udp_observed.HasValue(),
    "DDS remote UDP cache did not observe local publication");

  const auto local_udp_publication_matches = local_udp_cache.MatchesFor(
    mapping,
    DdsEndpointRole::kPublication,
    1'320U,
    500U);
  const auto remote_udp_subscription_matches = remote_udp_cache.MatchesFor(
    remote_mapping,
    DdsEndpointRole::kSubscription,
    1'320U,
    500U);
  Require(
    local_udp_publication_matches.HasValue() &&
      remote_udp_subscription_matches.HasValue(),
    "DDS best-effort UDP match lookup failed");
  Require(
    local_udp_publication_matches.Value().size() == 1U &&
      remote_udp_subscription_matches.Value().size() == 1U,
    "DDS best-effort UDP match count changed");

  DdsBestEffortWriter udp_writer(mapping);
  DdsBestEffortReader udp_reader(remote_mapping);
  const auto udp_sent = udp_writer.PublishSample(
    best_effort_sender.Value(),
    local_udp_publication_matches.Value()[0U],
    sample);
  Require(udp_sent.HasValue(), "DDS best-effort UDP publish failed");
  const auto udp_received = udp_reader.ReceiveSample(
    best_effort_receiver.Value(),
    remote_udp_subscription_matches.Value()[0U],
    1'330U);
  Require(udp_received.HasValue(), "DDS best-effort UDP receive failed");
  Require(
    udp_received.Value().payload == sample.payload,
    "DDS best-effort UDP payload changed");
  Require(
    udp_received.Value().sequence == 1U,
    "DDS best-effort UDP sequence changed");

  const auto local_reliable_publication_discovery = DdsBinding::BuildEndpointDiscovery(
    reliable_mapping,
    DdsEndpointRole::kPublication,
    {.host = "127.0.0.1", .port = 7460U},
    19U);
  const auto remote_reliable_subscription_discovery = DdsBinding::BuildEndpointDiscovery(
    remote_reliable_mapping,
    DdsEndpointRole::kSubscription,
    {.host = "10.0.0.60", .port = 7461U},
    19U);
  Require(
    local_reliable_publication_discovery.HasValue() &&
      remote_reliable_subscription_discovery.HasValue(),
    "DDS reliable endpoint discoveries did not build");

  Require(
    remote_discovery_cache
      .ObserveEndpointDiscovery(
        DdsEndpointRole::kPublication,
        local_reliable_publication_discovery.Value(),
        1'340U)
      .HasValue(),
    "DDS remote cache did not observe local reliable publication");
  Require(
    discovery_cache
      .ObserveEndpointDiscovery(
        DdsEndpointRole::kSubscription,
        remote_reliable_subscription_discovery.Value(),
        1'340U)
      .HasValue(),
    "DDS local cache did not observe remote reliable subscription");

  const auto reliable_publication_matches = discovery_cache.MatchesFor(
    reliable_mapping,
    DdsEndpointRole::kPublication,
    1'345U,
    500U);
  Require(
    reliable_publication_matches.HasValue(),
    "DDS reliable publication match lookup failed");
  Require(
    reliable_publication_matches.Value().size() == 1U,
    "DDS reliable publication match count changed");
  Require(
    reliable_publication_matches.Value()[0U].remote_endpoint.reliability_kind ==
      rtps::kSedpReliabilityReliable,
    "DDS reliable publication matched a non-reliable endpoint");

  const auto remote_reliable_subscription_matches = remote_discovery_cache.MatchesFor(
    remote_reliable_mapping,
    DdsEndpointRole::kSubscription,
    1'345U,
    500U);
  Require(
    remote_reliable_subscription_matches.HasValue(),
    "DDS remote reliable subscription match lookup failed");
  Require(
    remote_reliable_subscription_matches.Value().size() == 1U,
    "DDS remote reliable subscription match count changed");

  DdsReliableWriter wrong_profile_reliable_writer(mapping, 4U);
  Require(
    !wrong_profile_reliable_writer.BuildSample(publication_matches.Value()[0U], sample)
       .HasValue(),
    "DDS reliable writer accepted a best-effort QoS profile");
  DdsBestEffortWriter wrong_profile_best_effort_writer(reliable_mapping);
  Require(
    !wrong_profile_best_effort_writer
       .BuildSample(reliable_publication_matches.Value()[0U], sample)
       .HasValue(),
    "DDS best-effort writer accepted a reliable QoS profile");

  DdsReliableWriter reliable_writer(reliable_mapping, 4U);
  const auto reliable_first = reliable_writer.BuildSample(
    reliable_publication_matches.Value()[0U],
    sample);
  const auto reliable_second = reliable_writer.BuildSample(
    reliable_publication_matches.Value()[0U],
    sample);
  const auto reliable_third = reliable_writer.BuildSample(
    reliable_publication_matches.Value()[0U],
    sample);
  Require(
    reliable_first.HasValue() && reliable_second.HasValue() && reliable_third.HasValue(),
    "DDS reliable writer did not build DATA history");
  Require(
    reliable_third.Value().data[0U].writer_sequence_number == 3U,
    "DDS reliable writer sequence did not advance");

  DdsReliableReader reliable_reader(remote_reliable_mapping);
  const auto reliable_accept_first = reliable_reader.AcceptSample(
    remote_reliable_subscription_matches.Value()[0U],
    reliable_first.Value(),
    1'350U);
  const auto reliable_accept_third = reliable_reader.AcceptSample(
    remote_reliable_subscription_matches.Value()[0U],
    reliable_third.Value(),
    1'360U);
  Require(
    reliable_accept_first.HasValue() && reliable_accept_third.HasValue(),
    "DDS reliable reader did not accept non-contiguous DATA");
  const auto reliable_duplicate = reliable_reader.AcceptSample(
    remote_reliable_subscription_matches.Value()[0U],
    reliable_third.Value(),
    1'370U);
  Require(!reliable_duplicate.HasValue(), "DDS reliable reader accepted duplicate DATA");

  const auto reliable_heartbeat = reliable_writer.BuildHeartbeat(
    reliable_publication_matches.Value()[0U],
    false);
  Require(reliable_heartbeat.HasValue(), "DDS reliable writer did not build HEARTBEAT");
  Require(
    reliable_heartbeat.Value().heartbeats[0U].first_sequence_number == 1U,
    "DDS reliable HEARTBEAT first sequence changed");
  Require(
    reliable_heartbeat.Value().heartbeats[0U].last_sequence_number == 3U,
    "DDS reliable HEARTBEAT last sequence changed");

  const auto reliable_acknack = reliable_reader.BuildAckNack(
    remote_reliable_subscription_matches.Value()[0U],
    reliable_heartbeat.Value());
  Require(reliable_acknack.HasValue(), "DDS reliable reader did not build ACKNACK");
  const std::vector<std::uint64_t> expected_missing_reliable_sample{2U};
  Require(
    reliable_acknack.Value().acknacks[0U].missing_sequence_numbers ==
      expected_missing_reliable_sample,
    "DDS reliable ACKNACK missing sequence changed");
  Require(
    !reliable_acknack.Value().acknacks[0U].final_flag,
    "DDS reliable ACKNACK final flag changed for missing sample");

  const auto reliable_repairs = reliable_writer.BuildRepairSamples(
    reliable_publication_matches.Value()[0U],
    reliable_acknack.Value());
  Require(reliable_repairs.HasValue(), "DDS reliable writer did not build repair samples");
  Require(reliable_repairs.Value().size() == 1U, "DDS reliable repair count changed");
  Require(
    reliable_repairs.Value()[0U].data[0U].writer_sequence_number == 2U,
    "DDS reliable repair sequence changed");

  const auto repaired_sample = reliable_reader.AcceptSample(
    remote_reliable_subscription_matches.Value()[0U],
    reliable_repairs.Value()[0U],
    1'380U);
  Require(repaired_sample.HasValue(), "DDS reliable reader rejected repaired DATA");

  const auto final_heartbeat = reliable_writer.BuildHeartbeat(
    reliable_publication_matches.Value()[0U],
    true);
  Require(final_heartbeat.HasValue(), "DDS reliable final HEARTBEAT did not build");
  const auto final_acknack = reliable_reader.BuildAckNack(
    remote_reliable_subscription_matches.Value()[0U],
    final_heartbeat.Value());
  Require(final_acknack.HasValue(), "DDS reliable final ACKNACK did not build");
  Require(
    final_acknack.Value().acknacks[0U].missing_sequence_numbers.empty(),
    "DDS reliable final ACKNACK reported missing samples");
  Require(
    final_acknack.Value().acknacks[0U].final_flag,
    "DDS reliable final ACKNACK flag changed");
  const auto no_repairs = reliable_writer.BuildRepairSamples(
    reliable_publication_matches.Value()[0U],
    final_acknack.Value());
  Require(no_repairs.HasValue(), "DDS reliable empty repair lookup failed");
  Require(no_repairs.Value().empty(), "DDS reliable empty repair produced DATA");

  const auto reliable_writer_snapshot = reliable_writer.Snapshot();
  Require(
    reliable_writer_snapshot.history_sequence_numbers == std::vector<std::uint64_t>{1U, 2U, 3U},
    "DDS reliable writer history changed");
  Require(
    reliable_writer_snapshot.heartbeat_count == 2U,
    "DDS reliable writer HEARTBEAT count changed");
  Require(
    reliable_writer_snapshot.repairs_sent == 1U,
    "DDS reliable writer repair count changed");
  Require(
    reliable_writer_snapshot.historical_replays_sent == 0U,
    "DDS reliable writer historical replay count changed");

  const auto reliable_reader_snapshot = reliable_reader.Snapshot();
  Require(
    reliable_reader_snapshot.received_sequence_numbers == std::vector<std::uint64_t>{1U, 2U, 3U},
    "DDS reliable reader sequence set changed");
  Require(
    reliable_reader_snapshot.duplicate_samples_rejected == 1U,
    "DDS reliable duplicate rejection count changed");
  Require(
    reliable_reader_snapshot.acknack_count == 2U,
    "DDS reliable ACKNACK count changed");
  Require(
    reliable_reader_snapshot.writer_states.size() == 1U,
    "DDS reliable writer state count changed");
  Require(
    reliable_reader_snapshot.writer_states[0U].last_sequence_number == 3U,
    "DDS reliable writer state sequence changed");

  DdsTopicMapping timed_reliable_mapping = remote_reliable_mapping;
  timed_reliable_mapping.qos.deadline_ms = 5U;
  timed_reliable_mapping.qos.liveliness_lease_duration_ms = 10U;
  DdsReliableReader timed_reliable_reader(timed_reliable_mapping);
  Require(
    timed_reliable_reader
      .AcceptSample(
        remote_reliable_subscription_matches.Value()[0U],
        reliable_first.Value(),
        5'000U)
      .HasValue(),
    "DDS timed reliable reader rejected first DATA");
  Require(
    timed_reliable_reader
      .AcceptSample(
        remote_reliable_subscription_matches.Value()[0U],
        reliable_third.Value(),
        5'010U)
      .HasValue(),
    "DDS timed reliable reader rejected deadline-late DATA");
  auto timed_reliable_snapshot = timed_reliable_reader.Snapshot();
  Require(
    timed_reliable_snapshot.deadline_misses == 1U,
    "DDS reliable deadline miss count changed");
  const auto removed_reliable_writers = timed_reliable_reader.RemoveExpiredWriters(5'025U);
  Require(
    removed_reliable_writers.HasValue() && removed_reliable_writers.Value() == 1U,
    "DDS reliable liveliness removal count changed");
  timed_reliable_snapshot = timed_reliable_reader.Snapshot();
  Require(
    timed_reliable_snapshot.liveliness_lost == 1U &&
      timed_reliable_snapshot.writer_states.empty(),
    "DDS reliable liveliness state changed");

  DdsTopicMapping cached_limited_reliable_mapping = remote_reliable_mapping;
  cached_limited_reliable_mapping.qos.history_depth = 2U;
  cached_limited_reliable_mapping.qos.max_cached_samples = 2U;
  DdsReliableReader cached_limited_reliable_reader(cached_limited_reliable_mapping);
  Require(
    cached_limited_reliable_reader
      .AcceptSample(
        remote_reliable_subscription_matches.Value()[0U],
        reliable_first.Value(),
        6'000U)
      .HasValue(),
    "DDS cached-limited reliable reader rejected first DATA");
  Require(
    cached_limited_reliable_reader
      .AcceptSample(
        remote_reliable_subscription_matches.Value()[0U],
        reliable_second.Value(),
        6'001U)
      .HasValue(),
    "DDS cached-limited reliable reader rejected second DATA");
  Require(
    !cached_limited_reliable_reader
       .AcceptSample(
         remote_reliable_subscription_matches.Value()[0U],
         reliable_third.Value(),
         6'002U)
       .HasValue(),
    "DDS cached-limited reliable reader accepted DATA above the resource limit");
  Require(
    cached_limited_reliable_reader.Snapshot().resource_limit_rejections == 1U,
    "DDS reliable cached-sample resource-limit rejection count changed");

  DdsTopicMapping writer_limited_reliable_mapping = reliable_mapping;
  writer_limited_reliable_mapping.qos.history_depth = 2U;
  writer_limited_reliable_mapping.qos.max_cached_samples = 2U;
  DdsReliableWriter writer_limited_reliable_writer(writer_limited_reliable_mapping, 3U);
  Require(
    !writer_limited_reliable_writer
       .BuildSample(reliable_publication_matches.Value()[0U], sample)
       .HasValue(),
    "DDS reliable writer accepted history depth above the cached-sample limit");
  Require(
    !reliable_writer.BuildHistoricalSamples(reliable_publication_matches.Value()[0U])
       .HasValue(),
    "DDS reliable writer replayed history for volatile durability QoS");

  DdsTopicMapping transient_reliable_mapping = reliable_mapping;
  transient_reliable_mapping.qos.durability = DdsDurabilityPolicy::kTransientLocal;
  DdsTopicMapping first_transient_reader_mapping = remote_reliable_mapping;
  first_transient_reader_mapping.qos = transient_reliable_mapping.qos;
  first_transient_reader_mapping.reader_id = {.value = {0x00U, 0x00U, 0x04U, 0x08U}};
  DdsDiscoveryCache first_transient_writer_cache;
  const auto first_transient_reader_discovery = DdsBinding::BuildEndpointDiscovery(
    first_transient_reader_mapping,
    DdsEndpointRole::kSubscription,
    {.host = "10.0.0.80", .port = 7480U},
    20U);
  Require(
    first_transient_reader_discovery.HasValue(),
    "DDS first transient-local reader discovery did not build");
  Require(
    first_transient_writer_cache
      .ObserveEndpointDiscovery(
        DdsEndpointRole::kSubscription,
        first_transient_reader_discovery.Value(),
        7'000U)
      .HasValue(),
    "DDS first transient-local reader was not observed");
  const auto first_transient_publication_matches = first_transient_writer_cache.MatchesFor(
    transient_reliable_mapping,
    DdsEndpointRole::kPublication,
    7'001U,
    500U);
  Require(
    first_transient_publication_matches.HasValue() &&
      first_transient_publication_matches.Value().size() == 1U,
    "DDS first transient-local publication match count changed");

  DdsReliableWriter transient_writer(transient_reliable_mapping, 4U);
  Require(
    transient_writer
      .BuildSample(first_transient_publication_matches.Value()[0U], sample)
      .HasValue(),
    "DDS transient-local sample 1 did not build");
  Require(
    transient_writer
      .BuildSample(first_transient_publication_matches.Value()[0U], sample)
      .HasValue(),
    "DDS transient-local sample 2 did not build");
  Require(
    transient_writer
      .BuildSample(first_transient_publication_matches.Value()[0U], sample)
      .HasValue(),
    "DDS transient-local sample 3 did not build");

  DdsTopicMapping late_transient_reader_mapping = first_transient_reader_mapping;
  late_transient_reader_mapping.participant_guid_prefix.value[11U] = 0x07U;
  late_transient_reader_mapping.reader_id = {.value = {0x00U, 0x00U, 0x04U, 0x09U}};
  DdsDiscoveryCache late_transient_writer_cache;
  const auto late_transient_reader_discovery = DdsBinding::BuildEndpointDiscovery(
    late_transient_reader_mapping,
    DdsEndpointRole::kSubscription,
    {.host = "10.0.0.81", .port = 7481U},
    21U);
  Require(
    late_transient_reader_discovery.HasValue(),
    "DDS late transient-local reader discovery did not build");
  Require(
    late_transient_writer_cache
      .ObserveEndpointDiscovery(
        DdsEndpointRole::kSubscription,
        late_transient_reader_discovery.Value(),
        7'010U)
      .HasValue(),
    "DDS late transient-local reader was not observed");
  const auto late_transient_publication_matches = late_transient_writer_cache.MatchesFor(
    transient_reliable_mapping,
    DdsEndpointRole::kPublication,
    7'011U,
    500U);
  Require(
    late_transient_publication_matches.HasValue() &&
      late_transient_publication_matches.Value().size() == 1U,
    "DDS late transient-local publication match count changed");

  DdsDiscoveryCache late_transient_reader_cache;
  const auto transient_writer_discovery = DdsBinding::BuildEndpointDiscovery(
    transient_reliable_mapping,
    DdsEndpointRole::kPublication,
    {.host = "127.0.0.1", .port = 7482U},
    22U);
  Require(transient_writer_discovery.HasValue(), "DDS transient writer discovery failed");
  Require(
    late_transient_reader_cache
      .ObserveEndpointDiscovery(
        DdsEndpointRole::kPublication,
        transient_writer_discovery.Value(),
        7'012U)
      .HasValue(),
    "DDS late reader did not observe transient writer");
  const auto late_transient_subscription_matches = late_transient_reader_cache.MatchesFor(
    late_transient_reader_mapping,
    DdsEndpointRole::kSubscription,
    7'013U,
    500U);
  Require(
    late_transient_subscription_matches.HasValue() &&
      late_transient_subscription_matches.Value().size() == 1U,
    "DDS late transient-local subscription match count changed");

  const auto historical_samples = transient_writer.BuildHistoricalSamples(
    late_transient_publication_matches.Value()[0U]);
  Require(historical_samples.HasValue(), "DDS transient-local historical replay failed");
  Require(historical_samples.Value().size() == 3U, "DDS historical replay count changed");
  Require(
    historical_samples.Value()[0U].data[0U].reader_id == late_transient_reader_mapping.reader_id,
    "DDS historical replay did not retarget reader id");
  Require(
    historical_samples.Value()[2U].data[0U].writer_sequence_number == 3U,
    "DDS historical replay sequence range changed");

  DdsReliableReader late_transient_reader(late_transient_reader_mapping);
  for (const auto& historical_sample : historical_samples.Value()) {
    Require(
      late_transient_reader
        .AcceptSample(
          late_transient_subscription_matches.Value()[0U],
          historical_sample,
          7'020U)
        .HasValue(),
      "DDS late reader rejected a historical DATA sample");
  }
  Require(
    late_transient_reader.Snapshot().received_sequence_numbers ==
      std::vector<std::uint64_t>{1U, 2U, 3U},
    "DDS late reader historical sequence set changed");
  Require(
    transient_writer.Snapshot().historical_replays_sent == 3U,
    "DDS transient-local historical replay count changed");

  DdsTopicMapping shallow_reliable_mapping = reliable_mapping;
  shallow_reliable_mapping.qos.history_depth = 2U;
  DdsReliableWriter shallow_history_writer(shallow_reliable_mapping, 2U);
  Require(
    shallow_history_writer.BuildSample(reliable_publication_matches.Value()[0U], sample)
      .HasValue(),
    "DDS shallow reliable sample 1 did not build");
  Require(
    shallow_history_writer.BuildSample(reliable_publication_matches.Value()[0U], sample)
      .HasValue(),
    "DDS shallow reliable sample 2 did not build");
  Require(
    shallow_history_writer.BuildSample(reliable_publication_matches.Value()[0U], sample)
      .HasValue(),
    "DDS shallow reliable sample 3 did not build");
  const rtps::RtpsMessage old_sample_acknack{
    .vendor_id = {},
    .guid_prefix = remote_reliable_mapping.participant_guid_prefix,
    .acknacks = {{
      .reader_id = remote_reliable_mapping.reader_id,
      .writer_id = reliable_mapping.writer_id,
      .bitmap_base = 1U,
      .missing_sequence_numbers = {1U},
      .count = 1U,
    }},
  };
  Require(
    !shallow_history_writer
       .BuildRepairSamples(reliable_publication_matches.Value()[0U], old_sample_acknack)
       .HasValue(),
    "DDS reliable writer repaired a pruned history sample");

  Require(
    !discovery_cache.MatchesFor(mapping, DdsEndpointRole::kSubscription, 1'260U, 0U).HasValue(),
    "DDS zero discovery TTL was accepted");
  const auto removed_stale = discovery_cache.RemoveStaleEndpoints(2'000U, 500U);
  Require(removed_stale.HasValue(), "DDS stale endpoint removal failed");
  Require(removed_stale.Value() == 5U, "DDS stale endpoint removal count changed");
  const auto stale_matches = discovery_cache.MatchesFor(
    mapping,
    DdsEndpointRole::kSubscription,
    2'000U,
    500U);
  Require(stale_matches.HasValue(), "DDS stale match lookup failed");
  Require(stale_matches.Value().empty(), "DDS stale endpoint remained matched");

  return 0;
}
