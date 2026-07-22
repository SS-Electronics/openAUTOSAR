// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/com/service_registry.h"
#include "openautosar/core/result.h"
#include "openautosar/dds/rtps/endpoint_discovery.h"
#include "openautosar/dds/rtps/rtps_message.h"
#include "openautosar/dds/rtps/udp_endpoint.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openautosar::dds_binding {

enum class DdsReliabilityPolicy : std::uint8_t {
  kBestEffort,
  kReliable,
};

enum class DdsDurabilityPolicy : std::uint8_t {
  kVolatile,
  kTransientLocal,
};

enum class DdsHistoryPolicy : std::uint8_t {
  kKeepLast,
};

enum class DdsLivelinessPolicy : std::uint8_t {
  kAutomatic,
  kManualByParticipant,
};

enum class DdsOwnershipPolicy : std::uint8_t {
  kShared,
  kExclusive,
};

struct DdsQosProfile final {
  DdsReliabilityPolicy reliability{DdsReliabilityPolicy::kBestEffort};
  DdsDurabilityPolicy durability{DdsDurabilityPolicy::kVolatile};
  DdsHistoryPolicy history{DdsHistoryPolicy::kKeepLast};
  DdsLivelinessPolicy liveliness{DdsLivelinessPolicy::kAutomatic};
  DdsOwnershipPolicy ownership{DdsOwnershipPolicy::kShared};
  std::size_t history_depth{1U};
  std::uint64_t deadline_ms{0U};
  std::uint64_t liveliness_lease_duration_ms{0U};
  std::size_t max_cached_samples{0U};
  std::size_t max_remote_writers{0U};

  friend bool operator==(const DdsQosProfile&, const DdsQosProfile&) = default;
};

struct DdsTopicMapping final {
  com::ServiceIdentifier ara_service{};
  std::string topic_name;
  std::string type_name;
  std::string event_name;
  DdsQosProfile qos{};
  dds::rtps::GuidPrefix participant_guid_prefix{};
  dds::rtps::EntityId writer_id{.value = {0x00U, 0x00U, 0x03U, 0x02U}};
  dds::rtps::EntityId reader_id{.value = {0x00U, 0x00U, 0x04U, 0x07U}};

  friend bool operator==(const DdsTopicMapping&, const DdsTopicMapping&) = default;
};

struct DdsMethodMapping final {
  com::ServiceIdentifier ara_service{};
  std::string method_name;
  std::string request_topic_name;
  std::string response_topic_name;
  std::string request_type_name;
  std::string response_type_name;
  DdsQosProfile qos{};
  dds::rtps::GuidPrefix participant_guid_prefix{};
  dds::rtps::EntityId request_writer_id{.value = {0x00U, 0x00U, 0x05U, 0x02U}};
  dds::rtps::EntityId request_reader_id{.value = {0x00U, 0x00U, 0x06U, 0x07U}};
  dds::rtps::EntityId response_writer_id{.value = {0x00U, 0x00U, 0x07U, 0x02U}};
  dds::rtps::EntityId response_reader_id{.value = {0x00U, 0x00U, 0x08U, 0x07U}};

  friend bool operator==(const DdsMethodMapping&, const DdsMethodMapping&) = default;
};

struct DdsFieldMapping final {
  com::ServiceIdentifier ara_service{};
  std::string field_name;
  std::string topic_name;
  std::string type_name;
  DdsQosProfile qos{};
  dds::rtps::GuidPrefix participant_guid_prefix{};
  dds::rtps::EntityId writer_id{.value = {0x00U, 0x00U, 0x09U, 0x02U}};
  dds::rtps::EntityId reader_id{.value = {0x00U, 0x00U, 0x0AU, 0x07U}};

  friend bool operator==(const DdsFieldMapping&, const DdsFieldMapping&) = default;
};

struct UnsupportedFeature final {
  std::string feature;
  std::string reason;
};

enum class DdsEndpointRole : std::uint8_t {
  kPublication,
  kSubscription,
};

struct DdsDiscoveredEndpoint final {
  DdsEndpointRole role{DdsEndpointRole::kPublication};
  dds::rtps::GuidPrefix participant_guid_prefix{};
  dds::rtps::EntityId endpoint_id{};
  std::string topic_name;
  std::string type_name;
  dds::rtps::UdpEndpointAddress unicast_locator{};
  std::uint32_t reliability_kind{dds::rtps::kSedpReliabilityBestEffort};
  std::uint32_t durability_kind{dds::rtps::kSedpDurabilityVolatile};
  std::uint64_t last_sequence_number{0U};
  std::uint64_t last_seen_ms{0U};

  friend bool operator==(const DdsDiscoveredEndpoint&, const DdsDiscoveredEndpoint&) = default;
};

struct DdsEndpointMatch final {
  DdsEndpointRole local_role{DdsEndpointRole::kSubscription};
  dds::rtps::EntityId local_endpoint_id{};
  DdsDiscoveredEndpoint remote_endpoint{};

  friend bool operator==(const DdsEndpointMatch&, const DdsEndpointMatch&) = default;
};

class DdsDiscoveryCache final {
public:
  [[nodiscard]] core::Result<DdsDiscoveredEndpoint> ObserveEndpointDiscovery(
    DdsEndpointRole remote_role,
    const dds::rtps::RtpsMessage& message,
    std::uint64_t now_ms);

  [[nodiscard]] core::Result<std::vector<DdsEndpointMatch>> MatchesFor(
    const DdsTopicMapping& local_mapping,
    DdsEndpointRole local_role,
    std::uint64_t now_ms,
    std::uint64_t endpoint_ttl_ms) const;

  [[nodiscard]] core::Result<std::size_t> RemoveStaleEndpoints(
    std::uint64_t now_ms,
    std::uint64_t endpoint_ttl_ms);

private:
  std::vector<DdsDiscoveredEndpoint> endpoints_;
};

struct DdsWriterSnapshot final {
  std::uint64_t next_sequence_number{1U};
  std::uint64_t samples_sent{0U};

  friend bool operator==(const DdsWriterSnapshot&, const DdsWriterSnapshot&) = default;
};

class DdsBestEffortWriter final {
public:
  explicit DdsBestEffortWriter(DdsTopicMapping mapping);

  [[nodiscard]] core::Result<dds::rtps::RtpsMessage> BuildSample(
    const DdsEndpointMatch& match,
    const com::EventSample& sample);

  [[nodiscard]] core::Result<std::size_t> PublishSample(
    const dds::rtps::UdpEndpoint& endpoint,
    const DdsEndpointMatch& match,
    const com::EventSample& sample);

  [[nodiscard]] DdsWriterSnapshot Snapshot() const noexcept;

private:
  DdsTopicMapping mapping_;
  std::uint64_t next_sequence_number_{1U};
  std::uint64_t samples_sent_{0U};
};

struct DdsReaderSequenceState final {
  dds::rtps::GuidPrefix participant_guid_prefix{};
  dds::rtps::EntityId writer_id{};
  std::uint64_t last_sequence_number{0U};
  std::uint64_t last_seen_ms{0U};

  friend bool operator==(const DdsReaderSequenceState&, const DdsReaderSequenceState&) = default;
};

struct DdsReaderSnapshot final {
  std::uint64_t samples_received{0U};
  std::uint64_t stale_samples_rejected{0U};
  std::vector<DdsReaderSequenceState> writer_states;
  std::uint64_t deadline_misses{0U};
  std::uint64_t liveliness_lost{0U};
  std::uint64_t resource_limit_rejections{0U};

  friend bool operator==(const DdsReaderSnapshot&, const DdsReaderSnapshot&) = default;
};

class DdsBestEffortReader final {
public:
  explicit DdsBestEffortReader(DdsTopicMapping mapping);

  [[nodiscard]] core::Result<com::EventSample> AcceptSample(
    const DdsEndpointMatch& match,
    const dds::rtps::RtpsMessage& message,
    std::uint64_t now_ms);

  [[nodiscard]] core::Result<com::EventSample> ReceiveSample(
    const dds::rtps::UdpEndpoint& endpoint,
    const DdsEndpointMatch& match,
    std::uint64_t now_ms);

  [[nodiscard]] core::Result<std::size_t> RemoveExpiredWriters(std::uint64_t now_ms);

  [[nodiscard]] DdsReaderSnapshot Snapshot() const;

private:
  DdsTopicMapping mapping_;
  std::vector<DdsReaderSequenceState> writer_states_;
  std::uint64_t samples_received_{0U};
  std::uint64_t stale_samples_rejected_{0U};
  std::uint64_t deadline_misses_{0U};
  std::uint64_t liveliness_lost_{0U};
  std::uint64_t resource_limit_rejections_{0U};
};

struct DdsReliableWriterSnapshot final {
  std::uint64_t next_sequence_number{1U};
  std::uint64_t samples_sent{0U};
  std::uint64_t heartbeat_count{0U};
  std::uint64_t repairs_sent{0U};
  std::uint64_t historical_replays_sent{0U};
  std::vector<std::uint64_t> history_sequence_numbers;

  friend bool operator==(const DdsReliableWriterSnapshot&, const DdsReliableWriterSnapshot&) =
    default;
};

class DdsReliableWriter final {
public:
  DdsReliableWriter(DdsTopicMapping mapping, std::size_t history_depth);

  [[nodiscard]] core::Result<dds::rtps::RtpsMessage> BuildSample(
    const DdsEndpointMatch& match,
    const com::EventSample& sample);

  [[nodiscard]] core::Result<dds::rtps::RtpsMessage> BuildHeartbeat(
    const DdsEndpointMatch& match,
    bool final_flag);

  [[nodiscard]] core::Result<std::vector<dds::rtps::RtpsMessage>> BuildRepairSamples(
    const DdsEndpointMatch& match,
    const dds::rtps::RtpsMessage& acknack_message);

  [[nodiscard]] core::Result<std::vector<dds::rtps::RtpsMessage>> BuildHistoricalSamples(
    const DdsEndpointMatch& match);

  [[nodiscard]] DdsReliableWriterSnapshot Snapshot() const;

private:
  struct HistoryEntry final {
    std::uint64_t sequence_number{0U};
    dds::rtps::RtpsMessage message{};
  };

  DdsTopicMapping mapping_;
  std::size_t history_depth_{0U};
  std::vector<HistoryEntry> history_;
  std::uint64_t next_sequence_number_{1U};
  std::uint64_t samples_sent_{0U};
  std::uint64_t heartbeat_count_{0U};
  std::uint64_t repairs_sent_{0U};
  std::uint64_t historical_replays_sent_{0U};
};

struct DdsReliableReaderSnapshot final {
  std::uint64_t samples_received{0U};
  std::uint64_t duplicate_samples_rejected{0U};
  std::uint64_t acknack_count{0U};
  std::vector<std::uint64_t> received_sequence_numbers;
  std::vector<DdsReaderSequenceState> writer_states;
  std::uint64_t deadline_misses{0U};
  std::uint64_t liveliness_lost{0U};
  std::uint64_t resource_limit_rejections{0U};

  friend bool operator==(const DdsReliableReaderSnapshot&, const DdsReliableReaderSnapshot&) =
    default;
};

class DdsReliableReader final {
public:
  explicit DdsReliableReader(DdsTopicMapping mapping);

  [[nodiscard]] core::Result<com::EventSample> AcceptSample(
    const DdsEndpointMatch& match,
    const dds::rtps::RtpsMessage& message,
    std::uint64_t now_ms);

  [[nodiscard]] core::Result<dds::rtps::RtpsMessage> BuildAckNack(
    const DdsEndpointMatch& match,
    const dds::rtps::RtpsMessage& heartbeat_message);

  [[nodiscard]] core::Result<std::size_t> RemoveExpiredWriters(std::uint64_t now_ms);

  [[nodiscard]] DdsReliableReaderSnapshot Snapshot() const;

private:
  DdsTopicMapping mapping_;
  std::vector<std::uint64_t> received_sequence_numbers_;
  std::vector<DdsReaderSequenceState> writer_states_;
  std::uint64_t samples_received_{0U};
  std::uint64_t duplicate_samples_rejected_{0U};
  std::uint64_t acknack_count_{0U};
  std::uint64_t deadline_misses_{0U};
  std::uint64_t liveliness_lost_{0U};
  std::uint64_t resource_limit_rejections_{0U};
};

class DdsBinding final {
public:
  [[nodiscard]] static core::Result<dds::rtps::RtpsMessage> BuildDataMessage(
    const DdsTopicMapping& mapping,
    const com::EventSample& sample,
    std::uint64_t sequence_number);

  [[nodiscard]] static core::Result<com::EventSample> DecodeDataMessage(
    const DdsTopicMapping& mapping,
    const dds::rtps::RtpsMessage& message);

  [[nodiscard]] static core::Result<dds::rtps::RtpsMessage> BuildMethodRequest(
    const DdsMethodMapping& mapping,
    const com::MethodCall& call,
    std::uint64_t sequence_number);

  [[nodiscard]] static core::Result<com::MethodCall> DecodeMethodRequest(
    const DdsMethodMapping& mapping,
    const dds::rtps::RtpsMessage& message);

  [[nodiscard]] static core::Result<dds::rtps::RtpsMessage> BuildMethodResponse(
    const DdsMethodMapping& mapping,
    const com::MethodResult& result,
    std::uint64_t sequence_number);

  [[nodiscard]] static core::Result<com::MethodResult> DecodeMethodResponse(
    const DdsMethodMapping& mapping,
    const dds::rtps::RtpsMessage& message);

  [[nodiscard]] static core::Result<dds::rtps::RtpsMessage> BuildFieldNotification(
    const DdsFieldMapping& mapping,
    const com::FieldValue& value,
    std::uint64_t sequence_number);

  [[nodiscard]] static core::Result<com::FieldValue> DecodeFieldNotification(
    const DdsFieldMapping& mapping,
    const dds::rtps::RtpsMessage& message);

  [[nodiscard]] static core::Result<std::size_t> PublishEvent(
    const dds::rtps::UdpEndpoint& endpoint,
    dds::rtps::UdpEndpointAddress remote,
    const DdsTopicMapping& mapping,
    const com::EventSample& sample,
    std::uint64_t sequence_number);

  [[nodiscard]] static core::Result<com::EventSample> ReceiveEvent(
    const dds::rtps::UdpEndpoint& endpoint,
    const DdsTopicMapping& mapping);

  [[nodiscard]] static core::Result<std::size_t> PublishFieldNotification(
    const dds::rtps::UdpEndpoint& endpoint,
    dds::rtps::UdpEndpointAddress remote,
    const DdsFieldMapping& mapping,
    const com::FieldValue& value,
    std::uint64_t sequence_number);

  [[nodiscard]] static core::Result<com::FieldValue> ReceiveFieldNotification(
    const dds::rtps::UdpEndpoint& endpoint,
    const DdsFieldMapping& mapping);

  [[nodiscard]] static core::Result<dds::rtps::RtpsMessage> BuildEndpointDiscovery(
    const DdsTopicMapping& mapping,
    DdsEndpointRole role,
    dds::rtps::UdpEndpointAddress locator,
    std::uint64_t sequence_number);

  [[nodiscard]] static core::Result<dds::rtps::RtpsMessage> BuildEndpointDiscovery(
    const DdsFieldMapping& mapping,
    DdsEndpointRole role,
    dds::rtps::UdpEndpointAddress locator,
    std::uint64_t sequence_number);

  [[nodiscard]] static std::vector<UnsupportedFeature> UnsupportedFeatureMatrix();
};

}  // namespace openautosar::dds_binding
