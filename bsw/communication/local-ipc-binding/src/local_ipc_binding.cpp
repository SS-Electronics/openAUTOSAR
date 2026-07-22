// SPDX-License-Identifier: MIT

#include "openautosar/local_ipc/local_ipc_binding.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

namespace openautosar::local_ipc {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"local-ipc-binding", message};
}

[[nodiscard]] bool MatchesService(
  const com::ServiceIdentifier& left,
  const com::ServiceIdentifier& right) noexcept {
  return left == right;
}

[[nodiscard]] bool SamePeerIdentity(
  const PeerIdentity& left,
  const PeerIdentity& right) noexcept {
  return left.process_identity == right.process_identity &&
         left.machine_identity == right.machine_identity &&
         left.security_label == right.security_label &&
         left.uid == right.uid;
}

[[nodiscard]] bool SameMappingNames(
  const LocalIpcServiceMapping& left,
  const LocalIpcServiceMapping& right) noexcept {
  return left.event_name == right.event_name &&
         left.method_name == right.method_name &&
         left.field_name == right.field_name &&
         left.trigger_name == right.trigger_name;
}

[[nodiscard]] bool IsStale(
  const EndpointSnapshot& endpoint,
  std::uint64_t now_ms,
  std::uint32_t lease_timeout_ms) noexcept {
  return now_ms > endpoint.last_seen_ms &&
         now_ms - endpoint.last_seen_ms > static_cast<std::uint64_t>(lease_timeout_ms);
}

void FinalizeDeliveryReport(DeliveryReport& report) {
  if (report.backpressure_count == 0U) {
    return;
  }

  report.status = DeliveryStatus::kBackpressure;
  report.reason = "one or more local IPC queues are full";
}

[[nodiscard]] std::optional<LocalIpcFrame> PushFrameWithPolicy(
  std::deque<LocalIpcFrame>& queue,
  LocalIpcFrame frame,
  std::size_t queue_depth,
  QueuePolicy queue_policy,
  DeliveryReport& report) {
  if (queue.size() >= queue_depth) {
    if (queue_policy == QueuePolicy::kRejectNewest) {
      ++report.backpressure_count;
      return std::nullopt;
    }

    auto dropped = std::move(queue.front());
    queue.pop_front();
    ++report.dropped_count;
    queue.push_back(std::move(frame));
    ++report.delivered_count;
    return dropped;
  }

  queue.push_back(std::move(frame));
  ++report.delivered_count;
  return std::nullopt;
}

template <typename Predicate>
[[nodiscard]] std::optional<LocalIpcFrame> PopMatchingFrame(
  std::deque<LocalIpcFrame>& queue,
  Predicate predicate) {
  const auto iter = std::find_if(queue.begin(), queue.end(), predicate);
  if (iter == queue.end()) {
    return std::nullopt;
  }

  auto frame = std::move(*iter);
  queue.erase(iter);
  return frame;
}

}  // namespace

core::Result<EndpointSnapshot> LocalIpcBinding::OfferService(
  const com::ServiceOffer& offer,
  LocalIpcServiceMapping mapping,
  std::uint64_t now_ms) {
  auto validation = ValidateOffer(offer, mapping);
  if (!validation) {
    return core::Result<EndpointSnapshot>::FromError(validation.Error());
  }

  const auto key = ServiceKey(mapping.ara_service);
  auto iter = endpoints_.find(key);
  if (iter == endpoints_.end()) {
    EndpointState state{
      .mapping = std::move(mapping),
      .offered = true,
      .provider_generation = 1U,
      .last_seen_ms = now_ms,
      .subscriptions = {},
      .method_requests = {},
      .method_response_peers = {},
      .method_responses = {},
      .field_set_requests = {},
      .latest_field = std::nullopt,
    };
    auto [inserted, _] = endpoints_.emplace(key, std::move(state));
    return core::Result<EndpointSnapshot>::FromValue(SnapshotOf(inserted->second));
  }

  iter->second.mapping = std::move(mapping);
  iter->second.offered = true;
  iter->second.last_seen_ms = now_ms;
  return core::Result<EndpointSnapshot>::FromValue(SnapshotOf(iter->second));
}

core::Result<bool> LocalIpcBinding::StopOffer(const com::ServiceIdentifier& service) {
  auto iter = endpoints_.find(ServiceKey(service));
  if (iter == endpoints_.end() || !iter->second.offered) {
    return core::Result<bool>::FromError(MakeError("local IPC service is not offered"));
  }

  iter->second.offered = false;
  for (auto& [_, subscription] : iter->second.subscriptions) {
    subscription.queue.clear();
  }
  iter->second.method_requests.clear();
  iter->second.method_response_peers.clear();
  iter->second.method_responses.clear();
  iter->second.field_set_requests.clear();
  iter->second.latest_field = std::nullopt;
  return core::Result<bool>::FromValue(true);
}

core::Result<EndpointSnapshot> LocalIpcBinding::Subscribe(
  const LocalIpcServiceMapping& mapping,
  PeerIdentity consumer,
  std::size_t queue_depth,
  std::uint64_t now_ms) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<EndpointSnapshot>::FromError(validation.Error());
  }

  if (queue_depth == 0U || queue_depth > mapping.endpoint.default_queue_depth) {
    return core::Result<EndpointSnapshot>::FromError(
      MakeError("local IPC queue depth is outside endpoint bounds"));
  }

  if (!IsConsumerAllowed(mapping, consumer)) {
    return core::Result<EndpointSnapshot>::FromError(MakeError("local IPC peer is not allowed"));
  }

  auto iter = endpoints_.find(ServiceKey(mapping.ara_service));
  if (iter == endpoints_.end() || !iter->second.offered) {
    return core::Result<EndpointSnapshot>::FromError(MakeError("local IPC service is not offered"));
  }

  if (!MatchesService(iter->second.mapping.ara_service, mapping.ara_service) ||
      !SameMappingNames(iter->second.mapping, mapping)) {
    return core::Result<EndpointSnapshot>::FromError(
      MakeError("local IPC subscription mapping does not match offered endpoint"));
  }

  iter->second.last_seen_ms = now_ms;
  const auto peer_key = PeerKey(consumer);
  iter->second.subscriptions[peer_key] = SubscriptionState{
    .consumer = std::move(consumer),
    .queue_depth = queue_depth,
    .queue = {},
  };
  return core::Result<EndpointSnapshot>::FromValue(SnapshotOf(iter->second));
}

core::Result<DeliveryReport> LocalIpcBinding::PublishEvent(
  const LocalIpcServiceMapping& mapping,
  const com::EventSample& sample,
  const PeerIdentity& provider,
  std::uint64_t now_ms) {
  auto validation = ValidateSample(mapping, sample);
  if (!validation) {
    return core::Result<DeliveryReport>::FromError(validation.Error());
  }

  if (!SamePeerIdentity(provider, mapping.provider)) {
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kPeerRejected,
      .reason = "provider identity does not match local IPC mapping",
    });
  }

  auto iter = endpoints_.find(ServiceKey(mapping.ara_service));
  if (iter == endpoints_.end() || !iter->second.offered) {
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kNotOffered,
      .reason = "local IPC endpoint is not offered",
    });
  }

  if (hook_ == TestHook::kMarkEndpointStale) {
    iter->second.last_seen_ms = now_ms > mapping.endpoint.lease_timeout_ms
                                  ? now_ms - mapping.endpoint.lease_timeout_ms - 1U
                                  : 0U;
    hook_ = TestHook::kNone;
  }

  if (IsStale(SnapshotOf(iter->second), now_ms, mapping.endpoint.lease_timeout_ms)) {
    iter->second.offered = false;
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kStaleEndpoint,
      .reason = "local IPC endpoint lease expired",
    });
  }

  if (hook_ == TestHook::kDropNextFrame) {
    hook_ = TestHook::kNone;
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kDroppedByHook,
      .sequence = next_sequence_++,
      .dropped_count = iter->second.subscriptions.size(),
      .reason = "deterministic drop hook consumed frame",
    });
  }

  DeliveryReport report{
    .status = DeliveryStatus::kDelivered,
    .sequence = next_sequence_++,
    .delivered_count = 0U,
    .dropped_count = 0U,
    .backpressure_count = 0U,
    .reason = {},
  };

  iter->second.last_seen_ms = now_ms;
  for (auto& [_, subscription] : iter->second.subscriptions) {
    static_cast<void>(PushFrameWithPolicy(
      subscription.queue,
      {
        .type = FrameType::kEvent,
        .service = mapping.ara_service,
        .event_name = mapping.event_name,
        .method_name = {},
        .field_name = {},
        .trigger_name = {},
        .source = provider,
        .destination = subscription.consumer,
        .sequence = report.sequence,
        .timestamp_ms = now_ms,
        .provider_generation = iter->second.provider_generation,
        .correlation_id = 0U,
        .expects_response = true,
        .application_error = false,
        .error_domain = {},
        .error_code = 0U,
        .payload = sample.payload,
      },
      subscription.queue_depth,
      mapping.endpoint.queue_policy,
      report));
  }
  FinalizeDeliveryReport(report);

  return core::Result<DeliveryReport>::FromValue(std::move(report));
}

core::Result<LocalIpcFrame> LocalIpcBinding::PollEvent(
  const LocalIpcServiceMapping& mapping,
  const PeerIdentity& consumer,
  std::uint32_t timeout_ms,
  std::uint64_t now_ms) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<LocalIpcFrame>::FromError(validation.Error());
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC service is not offered"));
  }

  if (hook_ == TestHook::kForceTimeout) {
    hook_ = TestHook::kNone;
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC deterministic timeout"));
  }

  if (IsStale(SnapshotOf(endpoint->second), now_ms, mapping.endpoint.lease_timeout_ms)) {
    endpoint->second.offered = false;
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC endpoint is stale"));
  }

  auto subscription = endpoint->second.subscriptions.find(PeerKey(consumer));
  if (subscription == endpoint->second.subscriptions.end()) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC subscription is missing"));
  }

  auto frame = PopMatchingFrame(
    subscription->second.queue,
    [&mapping](const LocalIpcFrame& candidate) {
      return candidate.type == FrameType::kEvent &&
             candidate.event_name == mapping.event_name;
    });
  if (!frame.has_value()) {
    if (timeout_ms > 0U) {
      return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC poll timed out"));
    }
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC queue is empty"));
  }

  endpoint->second.last_seen_ms = now_ms;
  return core::Result<LocalIpcFrame>::FromValue(std::move(frame.value()));
}

core::Result<DeliveryReport> LocalIpcBinding::SubmitMethodRequest(
  const LocalIpcServiceMapping& mapping,
  const com::MethodCall& call,
  const PeerIdentity& consumer,
  std::uint64_t now_ms) {
  auto validation = ValidateMethodCall(mapping, call);
  if (!validation) {
    return core::Result<DeliveryReport>::FromError(validation.Error());
  }

  if (!IsConsumerAllowed(mapping, consumer)) {
    return core::Result<DeliveryReport>::FromError(MakeError("local IPC peer is not allowed"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kNotOffered,
      .reason = "local IPC endpoint is not offered",
    });
  }

  if (IsStale(SnapshotOf(endpoint->second), now_ms, mapping.endpoint.lease_timeout_ms)) {
    endpoint->second.offered = false;
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kStaleEndpoint,
      .reason = "local IPC endpoint lease expired",
    });
  }

  if (call.expects_response &&
      endpoint->second.method_response_peers.contains(call.correlation_id)) {
    return core::Result<DeliveryReport>::FromError(
      MakeError("local IPC method correlation is already active"));
  }

  if (hook_ == TestHook::kDropNextFrame) {
    hook_ = TestHook::kNone;
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kDroppedByHook,
      .sequence = next_sequence_++,
      .dropped_count = 1U,
      .reason = "deterministic drop hook consumed frame",
    });
  }

  DeliveryReport report{
    .status = DeliveryStatus::kDelivered,
    .sequence = next_sequence_++,
    .delivered_count = 0U,
    .dropped_count = 0U,
    .backpressure_count = 0U,
    .reason = {},
  };

  auto dropped = PushFrameWithPolicy(
    endpoint->second.method_requests,
    {
      .type = call.expects_response ? FrameType::kMethodRequest
                                    : FrameType::kFireAndForgetMethodRequest,
      .service = mapping.ara_service,
      .event_name = {},
      .method_name = mapping.method_name,
      .field_name = {},
      .trigger_name = {},
      .source = consumer,
      .destination = mapping.provider,
      .sequence = report.sequence,
      .timestamp_ms = now_ms,
      .provider_generation = endpoint->second.provider_generation,
      .correlation_id = call.correlation_id,
      .expects_response = call.expects_response,
      .application_error = false,
      .error_domain = {},
      .error_code = 0U,
      .payload = call.payload,
    },
    mapping.endpoint.default_queue_depth,
    mapping.endpoint.queue_policy,
    report);
  if (dropped.has_value() && dropped->expects_response) {
    endpoint->second.method_response_peers.erase(dropped->correlation_id);
  }
  if (report.delivered_count > 0U && call.expects_response) {
    endpoint->second.method_response_peers.emplace(call.correlation_id, consumer);
  }

  endpoint->second.last_seen_ms = now_ms;
  FinalizeDeliveryReport(report);
  return core::Result<DeliveryReport>::FromValue(std::move(report));
}

core::Result<LocalIpcFrame> LocalIpcBinding::TakeMethodRequest(
  const LocalIpcServiceMapping& mapping,
  const PeerIdentity& provider,
  std::uint32_t timeout_ms,
  std::uint64_t now_ms) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<LocalIpcFrame>::FromError(validation.Error());
  }

  if (mapping.method_name.empty()) {
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC method name is empty"));
  }

  if (!SamePeerIdentity(provider, mapping.provider)) {
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("provider identity does not match local IPC mapping"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC service is not offered"));
  }

  if (hook_ == TestHook::kForceTimeout) {
    hook_ = TestHook::kNone;
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC deterministic timeout"));
  }

  if (IsStale(SnapshotOf(endpoint->second), now_ms, mapping.endpoint.lease_timeout_ms)) {
    endpoint->second.offered = false;
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC endpoint is stale"));
  }

  auto frame = PopMatchingFrame(
    endpoint->second.method_requests,
    [&mapping](const LocalIpcFrame& candidate) {
      return (candidate.type == FrameType::kMethodRequest ||
              candidate.type == FrameType::kFireAndForgetMethodRequest) &&
             candidate.method_name == mapping.method_name;
    });
  if (!frame.has_value()) {
    if (timeout_ms > 0U) {
      return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC poll timed out"));
    }
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC method request queue is empty"));
  }

  endpoint->second.last_seen_ms = now_ms;
  return core::Result<LocalIpcFrame>::FromValue(std::move(frame.value()));
}

core::Result<DeliveryReport> LocalIpcBinding::CompleteMethodResponse(
  const LocalIpcServiceMapping& mapping,
  const com::MethodResult& result,
  const PeerIdentity& provider,
  std::uint64_t now_ms) {
  auto validation = ValidateMethodResult(mapping, result);
  if (!validation) {
    return core::Result<DeliveryReport>::FromError(validation.Error());
  }

  if (!SamePeerIdentity(provider, mapping.provider)) {
    return core::Result<DeliveryReport>::FromError(
      MakeError("provider identity does not match local IPC mapping"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kNotOffered,
      .reason = "local IPC endpoint is not offered",
    });
  }

  auto peer = endpoint->second.method_response_peers.find(result.correlation_id);
  if (peer == endpoint->second.method_response_peers.end()) {
    return core::Result<DeliveryReport>::FromError(
      MakeError("local IPC method response correlation is not active"));
  }

  DeliveryReport report{
    .status = DeliveryStatus::kDelivered,
    .sequence = next_sequence_++,
    .delivered_count = 0U,
    .dropped_count = 0U,
    .backpressure_count = 0U,
    .reason = {},
  };
  const auto peer_key = PeerKey(peer->second);
  static_cast<void>(PushFrameWithPolicy(
    endpoint->second.method_responses[peer_key],
    {
      .type = FrameType::kMethodResponse,
      .service = mapping.ara_service,
      .event_name = {},
      .method_name = mapping.method_name,
      .field_name = {},
      .trigger_name = {},
      .source = provider,
      .destination = peer->second,
      .sequence = report.sequence,
      .timestamp_ms = now_ms,
      .provider_generation = endpoint->second.provider_generation,
      .correlation_id = result.correlation_id,
      .expects_response = true,
      .application_error = result.application_error,
      .error_domain = result.error_domain,
      .error_code = result.error_code,
      .payload = result.payload,
    },
    mapping.endpoint.default_queue_depth,
    mapping.endpoint.queue_policy,
    report));

  if (report.delivered_count > 0U) {
    endpoint->second.method_response_peers.erase(peer);
  }

  endpoint->second.last_seen_ms = now_ms;
  FinalizeDeliveryReport(report);
  return core::Result<DeliveryReport>::FromValue(std::move(report));
}

core::Result<LocalIpcFrame> LocalIpcBinding::PollMethodResponse(
  const LocalIpcServiceMapping& mapping,
  const PeerIdentity& consumer,
  std::uint64_t correlation_id,
  std::uint32_t timeout_ms,
  std::uint64_t now_ms) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<LocalIpcFrame>::FromError(validation.Error());
  }

  if (mapping.method_name.empty()) {
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC method name is empty"));
  }

  if (correlation_id == 0U) {
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC method correlation id is zero"));
  }

  if (!IsConsumerAllowed(mapping, consumer)) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC peer is not allowed"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC service is not offered"));
  }

  auto responses = endpoint->second.method_responses.find(PeerKey(consumer));
  if (responses == endpoint->second.method_responses.end()) {
    if (timeout_ms > 0U) {
      return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC poll timed out"));
    }
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC method response queue is empty"));
  }

  auto frame = PopMatchingFrame(
    responses->second,
    [&mapping, correlation_id](const LocalIpcFrame& candidate) {
      return candidate.type == FrameType::kMethodResponse &&
             candidate.method_name == mapping.method_name &&
             candidate.correlation_id == correlation_id;
    });
  if (!frame.has_value()) {
    if (timeout_ms > 0U) {
      return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC poll timed out"));
    }
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC method response is not queued"));
  }

  endpoint->second.last_seen_ms = now_ms;
  return core::Result<LocalIpcFrame>::FromValue(std::move(frame.value()));
}

core::Result<EndpointSnapshot> LocalIpcBinding::SubscribeField(
  const LocalIpcServiceMapping& mapping,
  PeerIdentity consumer,
  std::size_t queue_depth,
  std::uint64_t now_ms) {
  if (mapping.field_name.empty()) {
    return core::Result<EndpointSnapshot>::FromError(
      MakeError("local IPC field name is empty"));
  }

  return Subscribe(mapping, std::move(consumer), queue_depth, now_ms);
}

core::Result<DeliveryReport> LocalIpcBinding::UpdateField(
  const LocalIpcServiceMapping& mapping,
  const com::FieldValue& value,
  const PeerIdentity& provider,
  std::uint64_t now_ms) {
  auto validation = ValidateFieldValue(mapping, value);
  if (!validation) {
    return core::Result<DeliveryReport>::FromError(validation.Error());
  }

  if (!SamePeerIdentity(provider, mapping.provider)) {
    return core::Result<DeliveryReport>::FromError(
      MakeError("provider identity does not match local IPC mapping"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kNotOffered,
      .reason = "local IPC endpoint is not offered",
    });
  }

  if (IsStale(SnapshotOf(endpoint->second), now_ms, mapping.endpoint.lease_timeout_ms)) {
    endpoint->second.offered = false;
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kStaleEndpoint,
      .reason = "local IPC endpoint lease expired",
    });
  }

  endpoint->second.latest_field = value;
  DeliveryReport report{
    .status = DeliveryStatus::kDelivered,
    .sequence = next_sequence_++,
    .delivered_count = 0U,
    .dropped_count = 0U,
    .backpressure_count = 0U,
    .reason = {},
  };

  for (auto& [_, subscription] : endpoint->second.subscriptions) {
    static_cast<void>(PushFrameWithPolicy(
      subscription.queue,
      {
        .type = FrameType::kFieldValue,
        .service = mapping.ara_service,
        .event_name = {},
        .method_name = {},
        .field_name = mapping.field_name,
        .trigger_name = {},
        .source = provider,
        .destination = subscription.consumer,
        .sequence = report.sequence,
        .timestamp_ms = now_ms,
        .provider_generation = endpoint->second.provider_generation,
        .correlation_id = 0U,
        .expects_response = true,
        .application_error = false,
        .error_domain = {},
        .error_code = 0U,
        .payload = value.payload,
      },
      subscription.queue_depth,
      mapping.endpoint.queue_policy,
      report));
  }

  endpoint->second.last_seen_ms = now_ms;
  FinalizeDeliveryReport(report);
  return core::Result<DeliveryReport>::FromValue(std::move(report));
}

core::Result<com::FieldValue> LocalIpcBinding::GetField(
  const LocalIpcServiceMapping& mapping,
  const PeerIdentity& consumer,
  std::uint64_t now_ms) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<com::FieldValue>::FromError(validation.Error());
  }

  if (mapping.field_name.empty()) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("local IPC field name is empty"));
  }

  if (!IsConsumerAllowed(mapping, consumer)) {
    return core::Result<com::FieldValue>::FromError(MakeError("local IPC peer is not allowed"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<com::FieldValue>::FromError(MakeError("local IPC service is not offered"));
  }

  if (IsStale(SnapshotOf(endpoint->second), now_ms, mapping.endpoint.lease_timeout_ms)) {
    endpoint->second.offered = false;
    return core::Result<com::FieldValue>::FromError(MakeError("local IPC endpoint is stale"));
  }

  if (!endpoint->second.latest_field.has_value()) {
    return core::Result<com::FieldValue>::FromError(
      MakeError("local IPC field value is not available"));
  }

  endpoint->second.last_seen_ms = now_ms;
  return core::Result<com::FieldValue>::FromValue(endpoint->second.latest_field.value());
}

core::Result<DeliveryReport> LocalIpcBinding::SubmitFieldSetRequest(
  const LocalIpcServiceMapping& mapping,
  const com::FieldValue& value,
  const PeerIdentity& consumer,
  std::uint64_t now_ms) {
  auto validation = ValidateFieldValue(mapping, value);
  if (!validation) {
    return core::Result<DeliveryReport>::FromError(validation.Error());
  }

  if (!IsConsumerAllowed(mapping, consumer)) {
    return core::Result<DeliveryReport>::FromError(MakeError("local IPC peer is not allowed"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kNotOffered,
      .reason = "local IPC endpoint is not offered",
    });
  }

  DeliveryReport report{
    .status = DeliveryStatus::kDelivered,
    .sequence = next_sequence_++,
    .delivered_count = 0U,
    .dropped_count = 0U,
    .backpressure_count = 0U,
    .reason = {},
  };
  static_cast<void>(PushFrameWithPolicy(
    endpoint->second.field_set_requests,
    {
      .type = FrameType::kFieldSetRequest,
      .service = mapping.ara_service,
      .event_name = {},
      .method_name = {},
      .field_name = mapping.field_name,
      .trigger_name = {},
      .source = consumer,
      .destination = mapping.provider,
      .sequence = report.sequence,
      .timestamp_ms = now_ms,
      .provider_generation = endpoint->second.provider_generation,
      .correlation_id = 0U,
      .expects_response = true,
      .application_error = false,
      .error_domain = {},
      .error_code = 0U,
      .payload = value.payload,
    },
    mapping.endpoint.default_queue_depth,
    mapping.endpoint.queue_policy,
    report));

  endpoint->second.last_seen_ms = now_ms;
  FinalizeDeliveryReport(report);
  return core::Result<DeliveryReport>::FromValue(std::move(report));
}

core::Result<LocalIpcFrame> LocalIpcBinding::TakeFieldSetRequest(
  const LocalIpcServiceMapping& mapping,
  const PeerIdentity& provider,
  std::uint32_t timeout_ms,
  std::uint64_t now_ms) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<LocalIpcFrame>::FromError(validation.Error());
  }

  if (mapping.field_name.empty()) {
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC field name is empty"));
  }

  if (!SamePeerIdentity(provider, mapping.provider)) {
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("provider identity does not match local IPC mapping"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC service is not offered"));
  }

  auto frame = PopMatchingFrame(
    endpoint->second.field_set_requests,
    [&mapping](const LocalIpcFrame& candidate) {
      return candidate.type == FrameType::kFieldSetRequest &&
             candidate.field_name == mapping.field_name;
    });
  if (!frame.has_value()) {
    if (timeout_ms > 0U) {
      return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC poll timed out"));
    }
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC field set queue is empty"));
  }

  endpoint->second.last_seen_ms = now_ms;
  return core::Result<LocalIpcFrame>::FromValue(std::move(frame.value()));
}

core::Result<LocalIpcFrame> LocalIpcBinding::PollField(
  const LocalIpcServiceMapping& mapping,
  const PeerIdentity& consumer,
  std::uint32_t timeout_ms,
  std::uint64_t now_ms) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<LocalIpcFrame>::FromError(validation.Error());
  }

  if (mapping.field_name.empty()) {
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC field name is empty"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC service is not offered"));
  }

  auto subscription = endpoint->second.subscriptions.find(PeerKey(consumer));
  if (subscription == endpoint->second.subscriptions.end()) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC subscription is missing"));
  }

  auto frame = PopMatchingFrame(
    subscription->second.queue,
    [&mapping](const LocalIpcFrame& candidate) {
      return candidate.type == FrameType::kFieldValue &&
             candidate.field_name == mapping.field_name;
    });
  if (!frame.has_value()) {
    if (timeout_ms > 0U) {
      return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC poll timed out"));
    }
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC field queue is empty"));
  }

  endpoint->second.last_seen_ms = now_ms;
  return core::Result<LocalIpcFrame>::FromValue(std::move(frame.value()));
}

core::Result<EndpointSnapshot> LocalIpcBinding::SubscribeTrigger(
  const LocalIpcServiceMapping& mapping,
  PeerIdentity consumer,
  std::size_t queue_depth,
  std::uint64_t now_ms) {
  if (mapping.trigger_name.empty()) {
    return core::Result<EndpointSnapshot>::FromError(
      MakeError("local IPC trigger name is empty"));
  }

  return Subscribe(mapping, std::move(consumer), queue_depth, now_ms);
}

core::Result<DeliveryReport> LocalIpcBinding::FireTrigger(
  const LocalIpcServiceMapping& mapping,
  const com::TriggerActivation& activation,
  const PeerIdentity& provider,
  std::uint64_t now_ms) {
  auto validation = ValidateTriggerActivation(mapping, activation);
  if (!validation) {
    return core::Result<DeliveryReport>::FromError(validation.Error());
  }

  if (!SamePeerIdentity(provider, mapping.provider)) {
    return core::Result<DeliveryReport>::FromError(
      MakeError("provider identity does not match local IPC mapping"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<DeliveryReport>::FromValue({
      .status = DeliveryStatus::kNotOffered,
      .reason = "local IPC endpoint is not offered",
    });
  }

  DeliveryReport report{
    .status = DeliveryStatus::kDelivered,
    .sequence = next_sequence_++,
    .delivered_count = 0U,
    .dropped_count = 0U,
    .backpressure_count = 0U,
    .reason = {},
  };
  for (auto& [_, subscription] : endpoint->second.subscriptions) {
    static_cast<void>(PushFrameWithPolicy(
      subscription.queue,
      {
        .type = FrameType::kTrigger,
        .service = mapping.ara_service,
        .event_name = {},
        .method_name = {},
        .field_name = {},
        .trigger_name = mapping.trigger_name,
        .source = provider,
        .destination = subscription.consumer,
        .sequence = report.sequence,
        .timestamp_ms = now_ms,
        .provider_generation = endpoint->second.provider_generation,
        .correlation_id = 0U,
        .expects_response = false,
        .application_error = false,
        .error_domain = {},
        .error_code = 0U,
        .payload = {},
      },
      subscription.queue_depth,
      mapping.endpoint.queue_policy,
      report));
  }

  endpoint->second.last_seen_ms = now_ms;
  FinalizeDeliveryReport(report);
  return core::Result<DeliveryReport>::FromValue(std::move(report));
}

core::Result<LocalIpcFrame> LocalIpcBinding::PollTrigger(
  const LocalIpcServiceMapping& mapping,
  const PeerIdentity& consumer,
  std::uint32_t timeout_ms,
  std::uint64_t now_ms) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<LocalIpcFrame>::FromError(validation.Error());
  }

  if (mapping.trigger_name.empty()) {
    return core::Result<LocalIpcFrame>::FromError(
      MakeError("local IPC trigger name is empty"));
  }

  auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end() || !endpoint->second.offered) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC service is not offered"));
  }

  auto subscription = endpoint->second.subscriptions.find(PeerKey(consumer));
  if (subscription == endpoint->second.subscriptions.end()) {
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC subscription is missing"));
  }

  auto frame = PopMatchingFrame(
    subscription->second.queue,
    [&mapping](const LocalIpcFrame& candidate) {
      return candidate.type == FrameType::kTrigger &&
             candidate.trigger_name == mapping.trigger_name;
    });
  if (!frame.has_value()) {
    if (timeout_ms > 0U) {
      return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC poll timed out"));
    }
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC trigger queue is empty"));
  }

  endpoint->second.last_seen_ms = now_ms;
  return core::Result<LocalIpcFrame>::FromValue(std::move(frame.value()));
}

std::vector<EndpointSnapshot> LocalIpcBinding::CleanupStaleEndpoints(std::uint64_t now_ms) {
  std::vector<EndpointSnapshot> cleaned;
  for (auto& [_, endpoint] : endpoints_) {
    if (!endpoint.offered) {
      continue;
    }

    const auto snapshot = SnapshotOf(endpoint);
    if (!IsStale(snapshot, now_ms, endpoint.mapping.endpoint.lease_timeout_ms)) {
      continue;
    }

    endpoint.offered = false;
    for (auto& [__, subscription] : endpoint.subscriptions) {
      subscription.queue.clear();
    }
    endpoint.method_requests.clear();
    endpoint.method_response_peers.clear();
    endpoint.method_responses.clear();
    endpoint.field_set_requests.clear();
    cleaned.push_back(SnapshotOf(endpoint));
  }

  return cleaned;
}

core::Result<EndpointSnapshot> LocalIpcBinding::SimulateProviderRestart(
  const LocalIpcServiceMapping& mapping,
  std::uint64_t now_ms) {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return core::Result<EndpointSnapshot>::FromError(validation.Error());
  }

  auto iter = endpoints_.find(ServiceKey(mapping.ara_service));
  if (iter == endpoints_.end()) {
    return core::Result<EndpointSnapshot>::FromError(MakeError("local IPC endpoint is missing"));
  }

  iter->second.mapping = mapping;
  iter->second.offered = true;
  iter->second.last_seen_ms = now_ms;
  ++iter->second.provider_generation;
  for (auto& [_, subscription] : iter->second.subscriptions) {
    subscription.queue.clear();
  }
  iter->second.method_requests.clear();
  iter->second.method_response_peers.clear();
  iter->second.method_responses.clear();
  iter->second.field_set_requests.clear();
  return core::Result<EndpointSnapshot>::FromValue(SnapshotOf(iter->second));
}

void LocalIpcBinding::SetTestHook(TestHook hook) noexcept {
  hook_ = hook;
}

void LocalIpcBinding::ClearTestHook() noexcept {
  hook_ = TestHook::kNone;
}

std::vector<EndpointSnapshot> LocalIpcBinding::ActiveEndpoints() const {
  std::vector<EndpointSnapshot> snapshots;
  for (const auto& [_, endpoint] : endpoints_) {
    if (endpoint.offered) {
      snapshots.push_back(SnapshotOf(endpoint));
    }
  }
  return snapshots;
}

std::optional<EndpointSnapshot> LocalIpcBinding::FindEndpoint(
  const com::ServiceIdentifier& service) const {
  const auto iter = endpoints_.find(ServiceKey(service));
  if (iter == endpoints_.end()) {
    return std::nullopt;
  }

  return SnapshotOf(iter->second);
}

std::size_t LocalIpcBinding::QueueDepthFor(
  const LocalIpcServiceMapping& mapping,
  const PeerIdentity& consumer) const {
  const auto endpoint = endpoints_.find(ServiceKey(mapping.ara_service));
  if (endpoint == endpoints_.end()) {
    return 0U;
  }

  const auto subscription = endpoint->second.subscriptions.find(PeerKey(consumer));
  if (subscription == endpoint->second.subscriptions.end()) {
    return 0U;
  }

  return subscription->second.queue.size();
}

core::Result<bool> LocalIpcBinding::ValidateMapping(
  const LocalIpcServiceMapping& mapping) const {
  if (mapping.ara_service.interface_id == 0U || mapping.ara_service.instance_id == 0U) {
    return core::Result<bool>::FromError(MakeError("ARA service identifier is invalid"));
  }

  if (mapping.ara_service.major_version == 0U) {
    return core::Result<bool>::FromError(MakeError("service major version is invalid"));
  }

  if (mapping.event_name.empty() && mapping.method_name.empty() &&
      mapping.field_name.empty() && mapping.trigger_name.empty()) {
    return core::Result<bool>::FromError(MakeError("local IPC mapping has no service members"));
  }

  if (mapping.endpoint.socket_path.empty() || mapping.endpoint.socket_path.front() != '/') {
    return core::Result<bool>::FromError(
      MakeError("local IPC endpoint must be an absolute socket path"));
  }

  if (mapping.endpoint.default_queue_depth == 0U ||
      mapping.endpoint.default_queue_depth > 64U) {
    return core::Result<bool>::FromError(
      MakeError("local IPC default queue depth is outside supported bounds"));
  }

  if (mapping.endpoint.lease_timeout_ms == 0U) {
    return core::Result<bool>::FromError(MakeError("local IPC lease timeout is zero"));
  }

  if (mapping.provider.process_identity.empty() || mapping.provider.machine_identity.empty()) {
    return core::Result<bool>::FromError(MakeError("local IPC provider identity is incomplete"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> LocalIpcBinding::ValidateOffer(
  const com::ServiceOffer& offer,
  const LocalIpcServiceMapping& mapping) const {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return validation;
  }

  if (!MatchesService(offer.service, mapping.ara_service)) {
    return core::Result<bool>::FromError(
      MakeError("service offer does not match local IPC mapping"));
  }

  if (offer.endpoint.binding != com::Binding::kLocalIpc) {
    return core::Result<bool>::FromError(MakeError("service offer binding is not local IPC"));
  }

  if (offer.endpoint.address != mapping.endpoint.socket_path) {
    return core::Result<bool>::FromError(
      MakeError("service offer endpoint does not match local IPC socket path"));
  }

  if (offer.process_identity != mapping.provider.process_identity ||
      offer.machine_identity != mapping.provider.machine_identity) {
    return core::Result<bool>::FromError(
      MakeError("service offer identity does not match local IPC provider"));
  }

  if (offer.ttl_ms == 0U) {
    return core::Result<bool>::FromError(MakeError("service offer TTL is zero"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> LocalIpcBinding::ValidateSample(
  const LocalIpcServiceMapping& mapping,
  const com::EventSample& sample) const {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return validation;
  }

  if (!MatchesService(sample.service, mapping.ara_service)) {
    return core::Result<bool>::FromError(
      MakeError("event sample service does not match local IPC mapping"));
  }

  if (sample.event_name != mapping.event_name) {
    return core::Result<bool>::FromError(
      MakeError("event sample name does not match local IPC mapping"));
  }

  if (sample.payload.empty() || sample.payload.size() > kMaxLocalIpcPayloadBytes) {
    return core::Result<bool>::FromError(MakeError("local IPC payload size is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> LocalIpcBinding::ValidateMethodCall(
  const LocalIpcServiceMapping& mapping,
  const com::MethodCall& call) const {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return validation;
  }

  if (mapping.method_name.empty()) {
    return core::Result<bool>::FromError(MakeError("local IPC method name is empty"));
  }

  if (!MatchesService(call.service, mapping.ara_service)) {
    return core::Result<bool>::FromError(
      MakeError("method call service does not match local IPC mapping"));
  }

  if (call.method_name != mapping.method_name) {
    return core::Result<bool>::FromError(
      MakeError("method call name does not match local IPC mapping"));
  }

  if (call.correlation_id == 0U) {
    return core::Result<bool>::FromError(
      MakeError("local IPC method correlation id is zero"));
  }

  if (call.payload.size() > kMaxLocalIpcPayloadBytes) {
    return core::Result<bool>::FromError(MakeError("local IPC payload size is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> LocalIpcBinding::ValidateMethodResult(
  const LocalIpcServiceMapping& mapping,
  const com::MethodResult& result) const {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return validation;
  }

  if (mapping.method_name.empty()) {
    return core::Result<bool>::FromError(MakeError("local IPC method name is empty"));
  }

  if (!MatchesService(result.service, mapping.ara_service)) {
    return core::Result<bool>::FromError(
      MakeError("method result service does not match local IPC mapping"));
  }

  if (result.method_name != mapping.method_name) {
    return core::Result<bool>::FromError(
      MakeError("method result name does not match local IPC mapping"));
  }

  if (result.correlation_id == 0U) {
    return core::Result<bool>::FromError(
      MakeError("local IPC method correlation id is zero"));
  }

  if (!result.application_error &&
      (!result.error_domain.empty() || result.error_code != 0U)) {
    return core::Result<bool>::FromError(
      MakeError("successful local IPC method result carries error metadata"));
  }

  if (result.application_error && result.error_domain.empty() && result.error_code != 0U) {
    return core::Result<bool>::FromError(MakeError("local IPC method error domain is empty"));
  }

  if (result.payload.size() > kMaxLocalIpcPayloadBytes) {
    return core::Result<bool>::FromError(MakeError("local IPC payload size is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> LocalIpcBinding::ValidateFieldValue(
  const LocalIpcServiceMapping& mapping,
  const com::FieldValue& value) const {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return validation;
  }

  if (mapping.field_name.empty()) {
    return core::Result<bool>::FromError(MakeError("local IPC field name is empty"));
  }

  if (!MatchesService(value.service, mapping.ara_service)) {
    return core::Result<bool>::FromError(
      MakeError("field value service does not match local IPC mapping"));
  }

  if (value.field_name != mapping.field_name) {
    return core::Result<bool>::FromError(
      MakeError("field value name does not match local IPC mapping"));
  }

  if (value.payload.empty() || value.payload.size() > kMaxLocalIpcPayloadBytes) {
    return core::Result<bool>::FromError(MakeError("local IPC payload size is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> LocalIpcBinding::ValidateTriggerActivation(
  const LocalIpcServiceMapping& mapping,
  const com::TriggerActivation& activation) const {
  auto validation = ValidateMapping(mapping);
  if (!validation) {
    return validation;
  }

  if (mapping.trigger_name.empty()) {
    return core::Result<bool>::FromError(MakeError("local IPC trigger name is empty"));
  }

  if (!MatchesService(activation.service, mapping.ara_service)) {
    return core::Result<bool>::FromError(
      MakeError("trigger activation service does not match local IPC mapping"));
  }

  if (activation.trigger_name != mapping.trigger_name) {
    return core::Result<bool>::FromError(
      MakeError("trigger activation name does not match local IPC mapping"));
  }

  return core::Result<bool>::FromValue(true);
}

bool LocalIpcBinding::IsConsumerAllowed(
  const LocalIpcServiceMapping& mapping,
  const PeerIdentity& consumer) const {
  if (consumer.process_identity.empty() || consumer.machine_identity.empty()) {
    return false;
  }

  if (mapping.allowed_consumers.empty()) {
    return consumer.machine_identity == mapping.provider.machine_identity;
  }

  return std::any_of(
    mapping.allowed_consumers.begin(),
    mapping.allowed_consumers.end(),
    [&consumer](const PeerIdentity& allowed) {
      return SamePeerIdentity(allowed, consumer);
    });
}

EndpointSnapshot LocalIpcBinding::SnapshotOf(const EndpointState& state) const {
  return {
    .service = state.mapping.ara_service,
    .event_name = state.mapping.event_name,
    .method_name = state.mapping.method_name,
    .field_name = state.mapping.field_name,
    .trigger_name = state.mapping.trigger_name,
    .socket_path = state.mapping.endpoint.socket_path,
    .offered = state.offered,
    .provider_generation = state.provider_generation,
    .last_seen_ms = state.last_seen_ms,
    .subscriber_count = state.subscriptions.size(),
    .deployment_provenance = state.mapping.deployment_provenance,
  };
}

std::string LocalIpcBinding::ServiceKey(const com::ServiceIdentifier& service) {
  std::ostringstream key;
  key << service.interface_id << ':' << service.instance_id << ':'
      << service.major_version << ':' << service.minor_version;
  return key.str();
}

std::string LocalIpcBinding::PeerKey(const PeerIdentity& peer) {
  std::ostringstream key;
  key << peer.machine_identity << ':' << peer.process_identity << ':'
      << peer.security_label << ':' << peer.uid;
  return key.str();
}

std::string_view ToString(QueuePolicy policy) noexcept {
  switch (policy) {
    case QueuePolicy::kRejectNewest:
      return "RejectNewest";
    case QueuePolicy::kDropOldest:
      return "DropOldest";
  }

  return "Unknown";
}

std::string_view ToString(FrameType type) noexcept {
  switch (type) {
    case FrameType::kOffer:
      return "Offer";
    case FrameType::kSubscribe:
      return "Subscribe";
    case FrameType::kEvent:
      return "Event";
    case FrameType::kMethodRequest:
      return "MethodRequest";
    case FrameType::kMethodResponse:
      return "MethodResponse";
    case FrameType::kFireAndForgetMethodRequest:
      return "FireAndForgetMethodRequest";
    case FrameType::kFieldValue:
      return "FieldValue";
    case FrameType::kFieldSetRequest:
      return "FieldSetRequest";
    case FrameType::kTrigger:
      return "Trigger";
    case FrameType::kHeartbeat:
      return "Heartbeat";
    case FrameType::kStopOffer:
      return "StopOffer";
  }

  return "Unknown";
}

std::string_view ToString(DeliveryStatus status) noexcept {
  switch (status) {
    case DeliveryStatus::kDelivered:
      return "Delivered";
    case DeliveryStatus::kDroppedByHook:
      return "DroppedByHook";
    case DeliveryStatus::kBackpressure:
      return "Backpressure";
    case DeliveryStatus::kPeerRejected:
      return "PeerRejected";
    case DeliveryStatus::kTimeout:
      return "Timeout";
    case DeliveryStatus::kStaleEndpoint:
      return "StaleEndpoint";
    case DeliveryStatus::kNotOffered:
      return "NotOffered";
    case DeliveryStatus::kMalformed:
      return "Malformed";
  }

  return "Unknown";
}

std::string_view ToString(TestHook hook) noexcept {
  switch (hook) {
    case TestHook::kNone:
      return "None";
    case TestHook::kDropNextFrame:
      return "DropNextFrame";
    case TestHook::kForceTimeout:
      return "ForceTimeout";
    case TestHook::kMarkEndpointStale:
      return "MarkEndpointStale";
  }

  return "Unknown";
}

}  // namespace openautosar::local_ipc
