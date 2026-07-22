// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/com/service_registry.h"
#include "openautosar/core/result.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::local_ipc {

inline constexpr std::size_t kMaxLocalIpcPayloadBytes{64U * 1024U};

enum class QueuePolicy {
  kRejectNewest,
  kDropOldest,
};

enum class FrameType {
  kOffer,
  kSubscribe,
  kEvent,
  kHeartbeat,
  kStopOffer,
};

enum class DeliveryStatus {
  kDelivered,
  kDroppedByHook,
  kBackpressure,
  kPeerRejected,
  kTimeout,
  kStaleEndpoint,
  kNotOffered,
  kMalformed,
};

enum class TestHook {
  kNone,
  kDropNextFrame,
  kForceTimeout,
  kMarkEndpointStale,
};

struct PeerIdentity final {
  std::string process_identity;
  std::string machine_identity;
  std::string security_label;
  std::uint32_t uid{0U};

  friend bool operator==(const PeerIdentity&, const PeerIdentity&) = default;
};

struct LocalIpcEndpoint final {
  std::string socket_path;
  std::uint32_t permissions{0660U};
  std::uint32_t lease_timeout_ms{1'000U};
  std::size_t default_queue_depth{4U};
  QueuePolicy queue_policy{QueuePolicy::kRejectNewest};

  friend bool operator==(const LocalIpcEndpoint&, const LocalIpcEndpoint&) = default;
};

struct LocalIpcServiceMapping final {
  com::ServiceIdentifier ara_service{};
  std::string event_name;
  LocalIpcEndpoint endpoint{};
  PeerIdentity provider{};
  std::vector<PeerIdentity> allowed_consumers;
  std::string deployment_provenance;

  friend bool operator==(const LocalIpcServiceMapping&, const LocalIpcServiceMapping&) = default;
};

struct LocalIpcFrame final {
  FrameType type{FrameType::kEvent};
  com::ServiceIdentifier service{};
  std::string event_name;
  PeerIdentity source{};
  PeerIdentity destination{};
  std::uint64_t sequence{0U};
  std::uint64_t timestamp_ms{0U};
  std::uint64_t provider_generation{0U};
  std::vector<std::uint8_t> payload;

  friend bool operator==(const LocalIpcFrame&, const LocalIpcFrame&) = default;
};

struct DeliveryReport final {
  DeliveryStatus status{DeliveryStatus::kMalformed};
  std::uint64_t sequence{0U};
  std::size_t delivered_count{0U};
  std::size_t dropped_count{0U};
  std::size_t backpressure_count{0U};
  std::string reason;

  friend bool operator==(const DeliveryReport&, const DeliveryReport&) = default;
};

struct EndpointSnapshot final {
  com::ServiceIdentifier service{};
  std::string event_name;
  std::string socket_path;
  bool offered{false};
  std::uint64_t provider_generation{0U};
  std::uint64_t last_seen_ms{0U};
  std::size_t subscriber_count{0U};
  std::string deployment_provenance;

  friend bool operator==(const EndpointSnapshot&, const EndpointSnapshot&) = default;
};

class LocalIpcBinding final {
public:
  [[nodiscard]] core::Result<EndpointSnapshot> OfferService(
    const com::ServiceOffer& offer,
    LocalIpcServiceMapping mapping,
    std::uint64_t now_ms);

  [[nodiscard]] core::Result<bool> StopOffer(const com::ServiceIdentifier& service);

  [[nodiscard]] core::Result<EndpointSnapshot> Subscribe(
    const LocalIpcServiceMapping& mapping,
    PeerIdentity consumer,
    std::size_t queue_depth,
    std::uint64_t now_ms);

  [[nodiscard]] core::Result<DeliveryReport> PublishEvent(
    const LocalIpcServiceMapping& mapping,
    const com::EventSample& sample,
    const PeerIdentity& provider,
    std::uint64_t now_ms);

  [[nodiscard]] core::Result<LocalIpcFrame> PollEvent(
    const LocalIpcServiceMapping& mapping,
    const PeerIdentity& consumer,
    std::uint32_t timeout_ms,
    std::uint64_t now_ms);

  [[nodiscard]] std::vector<EndpointSnapshot> CleanupStaleEndpoints(std::uint64_t now_ms);
  [[nodiscard]] core::Result<EndpointSnapshot> SimulateProviderRestart(
    const LocalIpcServiceMapping& mapping,
    std::uint64_t now_ms);
  void SetTestHook(TestHook hook) noexcept;
  void ClearTestHook() noexcept;

  [[nodiscard]] std::vector<EndpointSnapshot> ActiveEndpoints() const;
  [[nodiscard]] std::optional<EndpointSnapshot> FindEndpoint(
    const com::ServiceIdentifier& service) const;
  [[nodiscard]] std::size_t QueueDepthFor(
    const LocalIpcServiceMapping& mapping,
    const PeerIdentity& consumer) const;

private:
  struct SubscriptionState final {
    PeerIdentity consumer;
    std::size_t queue_depth{1U};
    std::deque<LocalIpcFrame> queue;
  };

  struct EndpointState final {
    LocalIpcServiceMapping mapping;
    bool offered{false};
    std::uint64_t provider_generation{1U};
    std::uint64_t last_seen_ms{0U};
    std::map<std::string, SubscriptionState> subscriptions;
  };

  [[nodiscard]] core::Result<bool> ValidateMapping(
    const LocalIpcServiceMapping& mapping) const;
  [[nodiscard]] core::Result<bool> ValidateOffer(
    const com::ServiceOffer& offer,
    const LocalIpcServiceMapping& mapping) const;
  [[nodiscard]] core::Result<bool> ValidateSample(
    const LocalIpcServiceMapping& mapping,
    const com::EventSample& sample) const;
  [[nodiscard]] bool IsConsumerAllowed(
    const LocalIpcServiceMapping& mapping,
    const PeerIdentity& consumer) const;
  [[nodiscard]] EndpointSnapshot SnapshotOf(const EndpointState& state) const;
  [[nodiscard]] static std::string ServiceKey(const com::ServiceIdentifier& service);
  [[nodiscard]] static std::string PeerKey(const PeerIdentity& peer);

  std::map<std::string, EndpointState> endpoints_;
  TestHook hook_{TestHook::kNone};
  std::uint64_t next_sequence_{1U};
};

[[nodiscard]] std::string_view ToString(QueuePolicy policy) noexcept;
[[nodiscard]] std::string_view ToString(FrameType type) noexcept;
[[nodiscard]] std::string_view ToString(DeliveryStatus status) noexcept;
[[nodiscard]] std::string_view ToString(TestHook hook) noexcept;

}  // namespace openautosar::local_ipc
