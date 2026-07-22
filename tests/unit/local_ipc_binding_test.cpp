// SPDX-License-Identifier: MIT

#include "openautosar/local_ipc/local_ipc_binding.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

constexpr openautosar::com::ServiceIdentifier kUltrasonicService{
  .interface_id = 0x0A500001U,
  .instance_id = 0x00000001U,
  .major_version = 1U,
  .minor_version = 0U,
};

openautosar::local_ipc::PeerIdentity Provider() {
  return {
    .process_identity = "ultrasonic-provider",
    .machine_identity = "qemux86-64",
    .security_label = "openautosar.sensor.provider",
    .uid = 4242U,
  };
}

openautosar::local_ipc::PeerIdentity Consumer(std::string process = "dashboard") {
  return {
    .process_identity = std::move(process),
    .machine_identity = "qemux86-64",
    .security_label = "openautosar.dashboard.consumer",
    .uid = 4243U,
  };
}

openautosar::local_ipc::LocalIpcServiceMapping Mapping(
  openautosar::local_ipc::QueuePolicy queue_policy =
    openautosar::local_ipc::QueuePolicy::kRejectNewest) {
  return {
    .ara_service = kUltrasonicService,
    .event_name = "DistanceSample",
    .endpoint = {
      .socket_path = "/run/openautosar/ipc/ultrasonic-distance.sock",
      .permissions = 0660U,
      .lease_timeout_ms = 100U,
      .default_queue_depth = 2U,
      .queue_policy = queue_policy,
    },
    .provider = Provider(),
    .allowed_consumers = {Consumer()},
    .deployment_provenance = "local-ipc-binding-test",
  };
}

openautosar::com::ServiceOffer Offer() {
  return {
    .service = kUltrasonicService,
    .process_identity = Provider().process_identity,
    .machine_identity = Provider().machine_identity,
    .endpoint = {
      .binding = openautosar::com::Binding::kLocalIpc,
      .address = "/run/openautosar/ipc/ultrasonic-distance.sock",
    },
    .ttl_ms = 250U,
    .access_policy = "unit-test",
    .deployment_provenance = "local-ipc-binding-test",
  };
}

openautosar::com::EventSample Sample(std::uint8_t value) {
  return {
    .service = kUltrasonicService,
    .event_name = "DistanceSample",
    .payload = {0x01U, value},
  };
}

}  // namespace

int main() {
  namespace ipc = openautosar::local_ipc;

  ipc::LocalIpcBinding binding;
  const auto mapping = Mapping();
  const auto offer = binding.OfferService(Offer(), mapping, 1'000U);
  Require(offer.HasValue(), "valid local IPC offer was rejected");
  Require(offer.Value().socket_path == mapping.endpoint.socket_path, "socket path changed");
  Require(offer.Value().subscriber_count == 0U, "unexpected local IPC subscriber count");

  auto invalid_mapping = mapping;
  invalid_mapping.endpoint.socket_path = "relative.sock";
  Require(
    !binding.OfferService(Offer(), invalid_mapping, 1'001U).HasValue(),
    "relative local IPC socket path was accepted");

  const auto unauthorized = binding.Subscribe(mapping, Consumer("camera"), 1U, 1'002U);
  Require(!unauthorized.HasValue(), "unauthorized local IPC peer was accepted");

  const auto subscribed = binding.Subscribe(mapping, Consumer(), 1U, 1'003U);
  Require(subscribed.HasValue(), "authorized local IPC subscriber was rejected");
  Require(subscribed.Value().subscriber_count == 1U, "subscriber count did not update");

  const auto delivered =
    binding.PublishEvent(mapping, Sample(0x10U), Provider(), 1'004U);
  Require(delivered.HasValue(), "local IPC publish failed");
  Require(delivered.Value().status == ipc::DeliveryStatus::kDelivered, "publish not delivered");
  Require(delivered.Value().delivered_count == 1U, "delivered count changed");
  Require(binding.QueueDepthFor(mapping, Consumer()) == 1U, "queue depth did not update");

  const auto backpressure =
    binding.PublishEvent(mapping, Sample(0x11U), Provider(), 1'005U);
  Require(backpressure.HasValue(), "backpressure publish failed");
  Require(
    backpressure.Value().status == ipc::DeliveryStatus::kBackpressure,
    "full local IPC queue did not report backpressure");
  Require(backpressure.Value().backpressure_count == 1U, "backpressure count changed");

  const auto frame = binding.PollEvent(mapping, Consumer(), 0U, 1'006U);
  Require(frame.HasValue(), "local IPC poll did not return queued event");
  Require(frame.Value().payload == Sample(0x10U).payload, "local IPC payload changed");
  Require(frame.Value().provider_generation == 1U, "provider generation changed");

  const auto timeout = binding.PollEvent(mapping, Consumer(), 10U, 1'007U);
  Require(!timeout.HasValue(), "empty local IPC poll with timeout succeeded");

  binding.SetTestHook(ipc::TestHook::kDropNextFrame);
  const auto dropped = binding.PublishEvent(mapping, Sample(0x12U), Provider(), 1'008U);
  Require(dropped.HasValue(), "drop hook publish failed");
  Require(
    dropped.Value().status == ipc::DeliveryStatus::kDroppedByHook,
    "drop hook did not consume frame");

  const auto wrong_provider =
    binding.PublishEvent(mapping, Sample(0x13U), Consumer(), 1'009U);
  Require(wrong_provider.HasValue(), "wrong-provider publish failed to report status");
  Require(
    wrong_provider.Value().status == ipc::DeliveryStatus::kPeerRejected,
    "wrong provider identity was not rejected");

  const auto restarted = binding.SimulateProviderRestart(mapping, 1'010U);
  Require(restarted.HasValue(), "provider restart simulation failed");
  Require(restarted.Value().provider_generation == 2U, "provider generation did not increment");
  Require(binding.QueueDepthFor(mapping, Consumer()) == 0U, "restart did not clear queue");

  const auto post_restart =
    binding.PublishEvent(mapping, Sample(0x20U), Provider(), 1'011U);
  Require(post_restart.HasValue(), "publish after restart failed");
  const auto restarted_frame = binding.PollEvent(mapping, Consumer(), 0U, 1'012U);
  Require(restarted_frame.HasValue(), "poll after restart failed");
  Require(
    restarted_frame.Value().provider_generation == 2U,
    "poll after restart used old generation");

  const auto cleaned = binding.CleanupStaleEndpoints(1'200U);
  Require(cleaned.size() == 1U, "stale endpoint cleanup did not report endpoint");
  Require(!cleaned[0U].offered, "stale endpoint remained offered");
  Require(binding.ActiveEndpoints().empty(), "stale endpoint remained active");
  const auto not_offered =
    binding.PublishEvent(mapping, Sample(0x21U), Provider(), 1'201U);
  Require(not_offered.HasValue(), "publish after stale cleanup did not report status");
  Require(
    not_offered.Value().status == ipc::DeliveryStatus::kNotOffered,
    "publish after stale cleanup was not rejected");

  ipc::LocalIpcBinding drop_oldest_binding;
  const auto drop_mapping = Mapping(ipc::QueuePolicy::kDropOldest);
  Require(
    drop_oldest_binding.OfferService(Offer(), drop_mapping, 2'000U).HasValue(),
    "drop-oldest offer failed");
  Require(
    drop_oldest_binding.Subscribe(drop_mapping, Consumer(), 1U, 2'001U).HasValue(),
    "drop-oldest subscribe failed");
  Require(
    drop_oldest_binding.PublishEvent(drop_mapping, Sample(0x30U), Provider(), 2'002U).HasValue(),
    "drop-oldest first publish failed");
  const auto drop_report =
    drop_oldest_binding.PublishEvent(drop_mapping, Sample(0x31U), Provider(), 2'003U);
  Require(drop_report.HasValue(), "drop-oldest second publish failed");
  Require(drop_report.Value().dropped_count == 1U, "drop-oldest did not drop oldest frame");
  const auto newest = drop_oldest_binding.PollEvent(drop_mapping, Consumer(), 0U, 2'004U);
  Require(newest.HasValue(), "drop-oldest poll failed");
  Require(newest.Value().payload == Sample(0x31U).payload, "drop-oldest did not keep newest");

  Require(
    ipc::ToString(ipc::QueuePolicy::kDropOldest) == std::string_view("DropOldest"),
    "queue policy text changed");
  Require(
    ipc::ToString(ipc::DeliveryStatus::kBackpressure) == std::string_view("Backpressure"),
    "delivery status text changed");
  Require(
    ipc::ToString(ipc::TestHook::kForceTimeout) == std::string_view("ForceTimeout"),
    "test hook text changed");

  return 0;
}
