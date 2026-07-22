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
    .method_name = "GetDistanceStatistics",
    .field_name = "CalibrationMode",
    .trigger_name = "ObstacleCleared",
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
      .port = 0U,
    },
    .offer_state = openautosar::com::OfferState::kOffered,
    .health_state = openautosar::com::HealthState::kHealthy,
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
    .sequence = 0U,
  };
}

openautosar::com::MethodCall MethodCall(
  std::uint64_t correlation_id,
  bool expects_response = true) {
  return {
    .service = kUltrasonicService,
    .method_name = "GetDistanceStatistics",
    .payload = {0x40U, 0x01U},
    .correlation_id = correlation_id,
    .expects_response = expects_response,
  };
}

openautosar::com::MethodResult MethodResult(
  std::uint64_t correlation_id,
  bool application_error = false) {
  return {
    .service = kUltrasonicService,
    .method_name = "GetDistanceStatistics",
    .payload = application_error ? std::vector<std::uint8_t>{0xEEU}
                                 : std::vector<std::uint8_t>{0x00U, 0x2AU},
    .correlation_id = correlation_id,
    .application_error = application_error,
    .error_domain = application_error
                      ? std::string("UltrasonicDistanceService.GetDistanceStatistics")
                      : std::string(),
    .error_code = application_error ? 1U : 0U,
  };
}

openautosar::com::FieldValue FieldValue(std::uint8_t value) {
  return {
    .service = kUltrasonicService,
    .field_name = "CalibrationMode",
    .payload = {value},
    .sequence = static_cast<std::uint64_t>(value),
  };
}

openautosar::com::TriggerActivation TriggerActivation(std::uint64_t sequence) {
  return {
    .service = kUltrasonicService,
    .trigger_name = "ObstacleCleared",
    .sequence = sequence,
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

  const auto method_submitted =
    binding.SubmitMethodRequest(mapping, MethodCall(0x0100U), Consumer(), 1'008U);
  Require(method_submitted.HasValue(), "local IPC method request submit failed");
  Require(
    method_submitted.Value().status == ipc::DeliveryStatus::kDelivered,
    "local IPC method request was not delivered");
  const auto method_request = binding.TakeMethodRequest(mapping, Provider(), 0U, 1'009U);
  Require(method_request.HasValue(), "local IPC method request was not queued");
  Require(
    method_request.Value().type == ipc::FrameType::kMethodRequest,
    "local IPC method request frame type changed");
  Require(
    method_request.Value().correlation_id == 0x0100U,
    "local IPC method request correlation changed");

  const auto method_completed =
    binding.CompleteMethodResponse(mapping, MethodResult(0x0100U, true), Provider(), 1'010U);
  Require(method_completed.HasValue(), "local IPC method response completion failed");
  const auto method_response =
    binding.PollMethodResponse(mapping, Consumer(), 0x0100U, 0U, 1'011U);
  Require(method_response.HasValue(), "local IPC method response was not queued");
  Require(
    method_response.Value().type == ipc::FrameType::kMethodResponse,
    "local IPC method response frame type changed");
  Require(
    method_response.Value().application_error,
    "local IPC method application error flag changed");
  Require(
    method_response.Value().error_domain ==
      "UltrasonicDistanceService.GetDistanceStatistics",
    "local IPC method application error domain changed");
  Require(method_response.Value().error_code == 1U, "local IPC method error code changed");

  const auto fire_and_forget =
    binding.SubmitMethodRequest(mapping, MethodCall(0x0101U, false), Consumer(), 1'012U);
  Require(fire_and_forget.HasValue(), "local IPC fire-and-forget submit failed");
  const auto no_response_request =
    binding.TakeMethodRequest(mapping, Provider(), 0U, 1'013U);
  Require(no_response_request.HasValue(), "local IPC fire-and-forget request missing");
  Require(
    no_response_request.Value().type == ipc::FrameType::kFireAndForgetMethodRequest,
    "local IPC fire-and-forget frame type changed");
  Require(
    !binding.CompleteMethodResponse(mapping, MethodResult(0x0101U), Provider(), 1'014U)
       .HasValue(),
    "local IPC fire-and-forget accepted a method response");

  Require(
    binding.SubscribeField(mapping, Consumer(), 2U, 1'015U).HasValue(),
    "local IPC field subscription failed");
  const auto field_update = binding.UpdateField(mapping, FieldValue(0x02U), Provider(), 1'016U);
  Require(field_update.HasValue(), "local IPC field update failed");
  const auto current_field = binding.GetField(mapping, Consumer(), 1'017U);
  Require(current_field.HasValue(), "local IPC field getter failed");
  Require(current_field.Value().payload == FieldValue(0x02U).payload, "field getter changed");
  const auto field_frame = binding.PollField(mapping, Consumer(), 0U, 1'018U);
  Require(field_frame.HasValue(), "local IPC field notifier did not deliver");
  Require(
    field_frame.Value().type == ipc::FrameType::kFieldValue,
    "local IPC field notifier frame type changed");

  const auto field_set =
    binding.SubmitFieldSetRequest(mapping, FieldValue(0x03U), Consumer(), 1'019U);
  Require(field_set.HasValue(), "local IPC field setter submit failed");
  const auto field_set_request = binding.TakeFieldSetRequest(mapping, Provider(), 0U, 1'020U);
  Require(field_set_request.HasValue(), "local IPC field setter request missing");
  Require(
    field_set_request.Value().type == ipc::FrameType::kFieldSetRequest,
    "local IPC field setter frame type changed");

  Require(
    binding.SubscribeTrigger(mapping, Consumer(), 2U, 1'021U).HasValue(),
    "local IPC trigger subscription failed");
  const auto trigger_report =
    binding.FireTrigger(mapping, TriggerActivation(7U), Provider(), 1'022U);
  Require(trigger_report.HasValue(), "local IPC trigger fire failed");
  const auto trigger_frame = binding.PollTrigger(mapping, Consumer(), 0U, 1'023U);
  Require(trigger_frame.HasValue(), "local IPC trigger was not delivered");
  Require(
    trigger_frame.Value().type == ipc::FrameType::kTrigger,
    "local IPC trigger frame type changed");
  Require(trigger_frame.Value().payload.empty(), "local IPC trigger carried a payload");

  binding.SetTestHook(ipc::TestHook::kDropNextFrame);
  const auto dropped = binding.PublishEvent(mapping, Sample(0x12U), Provider(), 1'024U);
  Require(dropped.HasValue(), "drop hook publish failed");
  Require(
    dropped.Value().status == ipc::DeliveryStatus::kDroppedByHook,
    "drop hook did not consume frame");

  const auto wrong_provider =
    binding.PublishEvent(mapping, Sample(0x13U), Consumer(), 1'025U);
  Require(wrong_provider.HasValue(), "wrong-provider publish failed to report status");
  Require(
    wrong_provider.Value().status == ipc::DeliveryStatus::kPeerRejected,
    "wrong provider identity was not rejected");

  const auto restarted = binding.SimulateProviderRestart(mapping, 1'026U);
  Require(restarted.HasValue(), "provider restart simulation failed");
  Require(restarted.Value().provider_generation == 2U, "provider generation did not increment");
  Require(binding.QueueDepthFor(mapping, Consumer()) == 0U, "restart did not clear queue");

  const auto post_restart =
    binding.PublishEvent(mapping, Sample(0x20U), Provider(), 1'027U);
  Require(post_restart.HasValue(), "publish after restart failed");
  const auto restarted_frame = binding.PollEvent(mapping, Consumer(), 0U, 1'028U);
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
    ipc::ToString(ipc::FrameType::kMethodResponse) == std::string_view("MethodResponse"),
    "method response frame type text changed");
  Require(
    ipc::ToString(ipc::FrameType::kTrigger) == std::string_view("Trigger"),
    "trigger frame type text changed");
  Require(
    ipc::ToString(ipc::DeliveryStatus::kBackpressure) == std::string_view("Backpressure"),
    "delivery status text changed");
  Require(
    ipc::ToString(ipc::TestHook::kForceTimeout) == std::string_view("ForceTimeout"),
    "test hook text changed");

  return 0;
}
