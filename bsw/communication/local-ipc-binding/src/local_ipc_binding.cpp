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

[[nodiscard]] bool IsStale(
  const EndpointSnapshot& endpoint,
  std::uint64_t now_ms,
  std::uint32_t lease_timeout_ms) noexcept {
  return now_ms > endpoint.last_seen_ms &&
         now_ms - endpoint.last_seen_ms > static_cast<std::uint64_t>(lease_timeout_ms);
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
      iter->second.mapping.event_name != mapping.event_name) {
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
    if (subscription.queue.size() >= subscription.queue_depth) {
      if (mapping.endpoint.queue_policy == QueuePolicy::kRejectNewest) {
        ++report.backpressure_count;
        continue;
      }

      subscription.queue.pop_front();
      ++report.dropped_count;
    }

    subscription.queue.push_back({
      .type = FrameType::kEvent,
      .service = mapping.ara_service,
      .event_name = mapping.event_name,
      .source = provider,
      .destination = subscription.consumer,
      .sequence = report.sequence,
      .timestamp_ms = now_ms,
      .provider_generation = iter->second.provider_generation,
      .payload = sample.payload,
    });
    ++report.delivered_count;
  }

  if (report.backpressure_count > 0U) {
    report.status = DeliveryStatus::kBackpressure;
    report.reason = "one or more local IPC subscriber queues are full";
  }

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

  if (subscription->second.queue.empty()) {
    if (timeout_ms > 0U) {
      return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC poll timed out"));
    }
    return core::Result<LocalIpcFrame>::FromError(MakeError("local IPC queue is empty"));
  }

  auto frame = std::move(subscription->second.queue.front());
  subscription->second.queue.pop_front();
  endpoint->second.last_seen_ms = now_ms;
  return core::Result<LocalIpcFrame>::FromValue(std::move(frame));
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

  if (mapping.event_name.empty()) {
    return core::Result<bool>::FromError(MakeError("local IPC event name is empty"));
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
