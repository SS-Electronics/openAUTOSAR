// SPDX-License-Identifier: MIT

#include "openautosar/com/service_registry.h"
#include "openautosar/virtual_vehicle/classic_ultrasonic_pdu.h"
#include "openautosar/virtual_vehicle/socketcan_ultrasonic_frame.h"
#include "openautosar/virtual_vehicle/ultrasonic_sensor_model.h"
#include "openautosar/virtual_vehicle/ultrasonic_signal_validator.h"

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

openautosar::virtual_vehicle::UltrasonicSample NextRequired(
  openautosar::virtual_vehicle::DeterministicUltrasonicModel& model,
  openautosar::virtual_vehicle::UltrasonicFault fault =
    openautosar::virtual_vehicle::UltrasonicFault::kNone) {
  auto sample = model.Next(fault);
  Require(sample.has_value(), "expected an ultrasonic sample");
  return sample.value();
}

}  // namespace

int main() {
  using namespace openautosar::virtual_vehicle;

  DeterministicUltrasonicModel model({
    .sensor_id = 7U,
    .scenario = UltrasonicScenario::kNormalApproach,
    .start_distance_mm = 1'000U,
    .step_mm = 100U,
    .period_ns = 10'000'000ULL,
  });

  const auto first = NextRequired(model);
  const auto second = NextRequired(model);
  Require(first.distance_mm == 1'000U, "first approach sample distance changed");
  Require(second.distance_mm == 900U, "second approach sample distance changed");
  Require(first.alive_counter == 0U && second.alive_counter == 1U, "alive counter did not increment");

  const auto encoded = EncodeClassicUltrasonicPdu(first);
  const auto decoded = DecodeClassicUltrasonicPdu(encoded);
  Require(decoded.HasValue(), "encoded PDU did not decode");
  Require(decoded.Value() == first, "decoded sample differs from original");

  const auto frame = EncodeSocketCanUltrasonicFrame(first);
  Require(frame.can_id == kClassicUltrasonicCanId, "SocketCAN frame id changed");
  Require(frame.len == kClassicUltrasonicPduSize, "SocketCAN payload length changed");

  const auto decoded_frame = DecodeSocketCanUltrasonicFrame(frame);
  Require(decoded_frame.HasValue(), "SocketCAN frame did not decode");
  Require(decoded_frame.Value() == first, "SocketCAN sample differs from original");

  auto wrong_id_frame = frame;
  wrong_id_frame.can_id = 0x123U;
  Require(!DecodeSocketCanUltrasonicFrame(wrong_id_frame).HasValue(), "unexpected CAN id accepted");

  openautosar::com::ServiceRegistry registry;
  const openautosar::com::ServiceIdentifier ultrasonic_service{
    .interface_id = 0x0A500001U,
    .instance_id = 1U,
    .major_version = 1U,
    .minor_version = 0U,
  };

  const auto missing_subscribe = registry.Subscribe(ultrasonic_service, "DistanceSample", 2U);
  Require(!missing_subscribe.HasValue(), "subscription before offer was accepted");

  const auto offered = registry.OfferService({
    .service = ultrasonic_service,
    .process_identity = "virtual-vehicle-test",
    .machine_identity = "qemux86-64",
    .endpoint = {.binding = openautosar::com::Binding::kLocalIpc, .address = "local://test"},
    .ttl_ms = 500U,
    .access_policy = "test",
    .deployment_provenance = "unit-test",
  });
  Require(offered.HasValue(), "service offer failed");

  const auto found = registry.FindService(ultrasonic_service);
  Require(found.size() == 1U, "offered service was not found");

  const auto find_handle = registry.StartFind(ultrasonic_service);
  Require(find_handle.HasValue(), "start-find failed");

  const auto subscription = registry.Subscribe(ultrasonic_service, "DistanceSample", 2U);
  Require(subscription.HasValue(), "event subscription failed");

  const auto first_publish = registry.Publish({
    .service = ultrasonic_service,
    .event_name = "DistanceSample",
    .payload = std::vector<std::uint8_t>(frame.data, frame.data + frame.len),
  });
  Require(first_publish.HasValue(), "event publish failed");

  const auto delivered = registry.Poll(subscription.Value().id);
  Require(delivered.HasValue(), "event was not delivered");
  Require(delivered.Value().sequence == first_publish.Value(), "event sequence mismatch");
  Require(delivered.Value().payload.size() == kClassicUltrasonicPduSize, "event payload size changed");

  const auto stopped_find = registry.StopFind(find_handle.Value().id);
  Require(stopped_find.HasValue(), "stop-find failed");

  const auto stopped_offer = registry.StopOffer(ultrasonic_service);
  Require(stopped_offer.HasValue(), "stop-offer failed");
  Require(registry.FindService(ultrasonic_service).empty(), "stopped offer remained discoverable");

  UltrasonicSignalValidator validator({.max_sample_age_ns = 25'000'000ULL});
  const auto valid_report = validator.ValidateFrame(encoded, first.timestamp_ns + 1'000'000ULL);
  Require(valid_report.state == UltrasonicServiceState::kAvailable, "valid frame was not available");
  Require(valid_report.publish_distance_mm.has_value(), "valid frame did not publish distance");
  Require(valid_report.publish_distance_mm.value() == first.distance_mm, "published distance changed");

  const auto crc_sample = NextRequired(model, UltrasonicFault::kCrcError);
  const auto corrupted = EncodeSocketCanUltrasonicFrame(crc_sample, UltrasonicFault::kCrcError);
  const auto crc_report = validator.ValidateSocketCanFrame(corrupted, crc_sample.timestamp_ns);
  Require(ContainsFault(crc_report, ValidationFault::kCrcMismatch), "CRC fault was not detected");
  Require(!crc_report.publish_distance_mm.has_value(), "CRC fault published distance");

  const auto jumped = NextRequired(model, UltrasonicFault::kCounterJump);
  const auto jumped_report =
    validator.ValidateFrame(EncodeClassicUltrasonicPdu(jumped), jumped.timestamp_ns);
  Require(
    ContainsFault(jumped_report, ValidationFault::kAliveCounterJump),
    "alive counter jump was not detected");
  Require(jumped_report.request_degraded_state, "repeated fault did not request degraded state");

  DeterministicUltrasonicModel stale_model({
    .sensor_id = 8U,
    .scenario = UltrasonicScenario::kStationaryObstacle,
    .start_distance_mm = 700U,
    .period_ns = 10'000'000ULL,
  });
  UltrasonicSignalValidator stale_validator({.max_sample_age_ns = 25'000'000ULL});
  const auto stale_sample = NextRequired(stale_model, UltrasonicFault::kDelayedFrame);
  const auto stale_report =
    stale_validator.ValidateFrame(EncodeClassicUltrasonicPdu(stale_sample), 80'000'000ULL);
  Require(ContainsFault(stale_report, ValidationFault::kStaleSample), "stale frame was not detected");
  Require(!stale_report.publish_distance_mm.has_value(), "stale frame published distance");

  DeterministicUltrasonicModel invalid_model({
    .sensor_id = 9U,
    .scenario = UltrasonicScenario::kNormalRecede,
    .start_distance_mm = 600U,
  });
  UltrasonicSignalValidator invalid_validator;
  const auto no_echo = NextRequired(invalid_model, UltrasonicFault::kNoEcho);
  const auto invalid_report =
    invalid_validator.ValidateFrame(EncodeClassicUltrasonicPdu(no_echo), no_echo.timestamp_ns);
  Require(ContainsFault(invalid_report, ValidationFault::kOutOfRange), "no-echo range fault missing");
  Require(ContainsFault(invalid_report, ValidationFault::kInvalidQuality), "no-echo quality fault missing");
  Require(!invalid_report.publish_distance_mm.has_value(), "invalid sample published a distance");

  const auto dropped_report = invalid_validator.ObserveDroppedFrame();
  Require(ContainsFault(dropped_report, ValidationFault::kDroppedFrame), "dropped frame not recorded");
  Require(dropped_report.request_degraded_state, "dropped frame sequence did not degrade");

  return 0;
}
