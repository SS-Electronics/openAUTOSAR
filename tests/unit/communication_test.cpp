// SPDX-License-Identifier: MIT

#include "openautosar/com/service_registry.h"

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
  using namespace openautosar::com;

  ServiceRegistry registry;
  const ServiceIdentifier service{
    .interface_id = 0x0A500001U,
    .instance_id = 0x00000001U,
    .major_version = 1U,
    .minor_version = 0U,
  };

  const auto invalid_offer = registry.OfferService({});
  Require(!invalid_offer.HasValue(), "invalid service offer was accepted");

  const auto invalid_find = registry.StartFind({});
  Require(!invalid_find.HasValue(), "invalid start-find was accepted");

  const auto offer = registry.OfferService({
    .service = service,
    .process_identity = "communication-test-provider",
    .machine_identity = "qemux86-64",
    .endpoint = {.binding = Binding::kLocalIpc, .address = "local://communication-test"},
    .ttl_ms = 250U,
    .access_policy = "unit-test",
    .deployment_provenance = "communication-test",
  });
  Require(offer.HasValue(), "valid service offer was rejected");
  Require(
    ToString(offer.Value().endpoint.binding) == std::string_view("local-ipc"),
    "binding text changed");

  const auto find_handle = registry.StartFind(service);
  Require(find_handle.HasValue(), "start-find failed");
  Require(registry.FindService(service).size() == 1U, "offered service was not discoverable");

  const auto subscription = registry.Subscribe(service, "DistanceSample", 2U);
  Require(subscription.HasValue(), "valid subscription failed");

  const auto sequence1 = registry.Publish({
    .service = service,
    .event_name = "DistanceSample",
    .payload = {1U},
  });
  const auto sequence2 = registry.Publish({
    .service = service,
    .event_name = "DistanceSample",
    .payload = {2U},
  });
  const auto sequence3 = registry.Publish({
    .service = service,
    .event_name = "DistanceSample",
    .payload = {3U},
  });
  Require(sequence1.HasValue() && sequence2.HasValue() && sequence3.HasValue(), "publish failed");

  const auto delivered2 = registry.Poll(subscription.Value().id);
  const auto delivered3 = registry.Poll(subscription.Value().id);
  const auto empty = registry.Poll(subscription.Value().id);
  Require(delivered2.HasValue() && delivered3.HasValue(), "bounded queue did not deliver samples");
  Require(
    delivered2.Value().sequence == sequence2.Value(),
    "bounded queue did not drop oldest sample");
  Require(delivered3.Value().sequence == sequence3.Value(), "latest sample sequence changed");
  Require(!empty.HasValue(), "empty queue poll succeeded");

  Require(
    !registry.GetField(service, "CalibrationMode").HasValue(),
    "unset field value was available");
  const auto field_subscription = registry.SubscribeField(service, "CalibrationMode", 1U);
  Require(field_subscription.HasValue(), "valid field subscription failed");
  const auto field_value1 = registry.SetField({
    .service = service,
    .field_name = "CalibrationMode",
    .payload = {0x01U},
  });
  const auto field_value2 = registry.SetField({
    .service = service,
    .field_name = "CalibrationMode",
    .payload = {0x02U},
  });
  Require(field_value1.HasValue() && field_value2.HasValue(), "valid field set failed");
  Require(
    field_value2.Value().sequence > field_value1.Value().sequence,
    "field sequence did not advance");
  const auto latest_field = registry.GetField(service, "CalibrationMode");
  Require(latest_field.HasValue(), "latest field value was not readable");
  Require(latest_field.Value() == field_value2.Value(), "latest field value changed");
  const auto notified_field = registry.PollField(field_subscription.Value().id);
  Require(notified_field.HasValue(), "field notification was not delivered");
  Require(notified_field.Value() == field_value2.Value(), "field notification was not latest");
  Require(
    !registry.PollField(field_subscription.Value().id).HasValue(),
    "empty field notification queue delivered a value");
  Require(
    registry.Unsubscribe(field_subscription.Value().id).HasValue(),
    "field unsubscribe failed");
  Require(
    !registry.PollField(field_subscription.Value().id).HasValue(),
    "unsubscribed field queue remained active");

  const auto trigger_subscription = registry.SubscribeTrigger(service, "ObstacleCleared", 1U);
  Require(trigger_subscription.HasValue(), "valid trigger subscription failed");
  const auto trigger1 = registry.FireTrigger({
    .service = service,
    .trigger_name = "ObstacleCleared",
  });
  const auto trigger2 = registry.FireTrigger({
    .service = service,
    .trigger_name = "ObstacleCleared",
  });
  Require(trigger1.HasValue() && trigger2.HasValue(), "valid trigger fire failed");
  Require(
    trigger2.Value().sequence > trigger1.Value().sequence,
    "trigger sequence did not advance");
  const auto delivered_trigger = registry.PollTrigger(trigger_subscription.Value().id);
  Require(delivered_trigger.HasValue(), "trigger activation was not delivered");
  Require(
    delivered_trigger.Value() == trigger2.Value(),
    "trigger queue did not drop oldest activation");
  Require(
    !registry.PollTrigger(trigger_subscription.Value().id).HasValue(),
    "empty trigger queue delivered an activation");
  Require(
    registry.Unsubscribe(trigger_subscription.Value().id).HasValue(),
    "trigger unsubscribe failed");
  Require(
    !registry.PollTrigger(trigger_subscription.Value().id).HasValue(),
    "unsubscribed trigger queue remained active");

  const auto method_call = registry.SubmitMethodCall({
    .service = service,
    .method_name = "GetDistanceStatistics",
    .payload = {0x10U, 0x20U},
  });
  Require(method_call.HasValue(), "valid method call was rejected");
  Require(method_call.Value().correlation_id != 0U, "method correlation was not assigned");
  Require(
    !registry
       .TakeMethodResult(service, "GetDistanceStatistics", method_call.Value().correlation_id)
       .HasValue(),
    "method result was available before completion");

  const auto taken_call = registry.TakeMethodCall(service, "GetDistanceStatistics");
  Require(taken_call.HasValue(), "queued method call was not delivered");
  Require(
    taken_call.Value() == method_call.Value(),
    "queued method call changed while delivered to provider");
  Require(
    !registry
       .CompleteMethodCall({
         .service = service,
         .method_name = "OtherMethod",
         .payload = {0x7FU},
         .correlation_id = method_call.Value().correlation_id,
         .application_error = false,
         .error_domain = {},
         .error_code = 0U,
       })
       .HasValue(),
    "method result for wrong method was accepted");

  const auto completed_call = registry.CompleteMethodCall({
    .service = service,
    .method_name = "GetDistanceStatistics",
    .payload = {0x30U, 0x40U},
    .correlation_id = method_call.Value().correlation_id,
    .application_error = false,
    .error_domain = {},
    .error_code = 0U,
  });
  Require(completed_call.HasValue(), "method completion was rejected");
  Require(
    !registry.CompleteMethodCall(completed_call.Value()).HasValue(),
    "duplicate method completion was accepted");

  const auto method_result = registry.TakeMethodResult(
    service,
    "GetDistanceStatistics",
    method_call.Value().correlation_id);
  Require(method_result.HasValue(), "completed method result was not delivered");
  Require(method_result.Value().payload == std::vector<std::uint8_t>({0x30U, 0x40U}),
          "method result payload changed");
  Require(!method_result.Value().application_error, "successful method result marked error");
  Require(
    !registry
       .TakeMethodResult(service, "GetDistanceStatistics", method_call.Value().correlation_id)
       .HasValue(),
    "consumed method result was delivered again");

  const auto future_call = registry.SubmitMethodCallFuture({
    .service = service,
    .method_name = "GetDistanceStatistics",
    .payload = {0x55U},
  });
  Require(future_call.HasValue(), "valid future method call was rejected");
  Require(
    future_call.Value().call.correlation_id != 0U,
    "future method correlation was not assigned");
  Require(!future_call.Value().future.IsReady(), "future was ready before completion");
  Require(
    !future_call.Value().future.Get().HasValue(),
    "pending future returned a method result");
  const auto future_taken_call = registry.TakeMethodCall(service, "GetDistanceStatistics");
  Require(future_taken_call.HasValue(), "future method call was not delivered");
  Require(
    future_taken_call.Value() == future_call.Value().call,
    "future method call changed while delivered to provider");
  const auto future_completed_call = registry.CompleteMethodCall({
    .service = service,
    .method_name = "GetDistanceStatistics",
    .payload = {0x66U},
    .correlation_id = future_call.Value().call.correlation_id,
    .application_error = false,
    .error_domain = {},
    .error_code = 0U,
  });
  Require(future_completed_call.HasValue(), "future method completion was rejected");
  Require(future_call.Value().future.IsReady(), "future was not ready after completion");
  const auto future_result = future_call.Value().future.Get();
  Require(future_result.HasValue(), "future did not return the completed method result");
  Require(
    future_result.Value().payload == std::vector<std::uint8_t>({0x66U}),
    "future method result payload changed");
  Require(
    registry
      .TakeMethodResult(
        service,
        "GetDistanceStatistics",
        future_call.Value().call.correlation_id)
      .HasValue(),
    "future method result was not available through polling");

  const auto structured_error_call = registry.SubmitMethodCallFuture({
    .service = service,
    .method_name = "GetDistanceStatistics",
    .payload = {0x70U},
  });
  Require(structured_error_call.HasValue(), "structured-error method call was rejected");
  const auto structured_taken_call = registry.TakeMethodCall(service, "GetDistanceStatistics");
  Require(structured_taken_call.HasValue(), "structured-error method call was not delivered");
  const auto structured_error_result = registry.CompleteMethodCall({
    .service = service,
    .method_name = "GetDistanceStatistics",
    .payload = {0xEEU},
    .correlation_id = structured_error_call.Value().call.correlation_id,
    .application_error = true,
    .error_domain = "UltrasonicDistanceService.GetDistanceStatistics",
    .error_code = 1U,
  });
  Require(structured_error_result.HasValue(), "structured method error was rejected");
  const auto structured_future_result = structured_error_call.Value().future.Get();
  Require(structured_future_result.HasValue(), "structured method error future was empty");
  Require(
    structured_future_result.Value().application_error,
    "structured method error flag changed");
  Require(
    structured_future_result.Value().payload == std::vector<std::uint8_t>({0xEEU}),
    "structured method error payload changed");
  Require(
    structured_future_result.Value().error_domain ==
      "UltrasonicDistanceService.GetDistanceStatistics",
    "structured method error domain changed");
  Require(
    structured_future_result.Value().error_code == 1U,
    "structured method error code changed");
  const auto structured_polled_result = registry.TakeMethodResult(
    service,
    "GetDistanceStatistics",
    structured_error_call.Value().call.correlation_id);
  Require(structured_polled_result.HasValue(), "structured method error was not queued");
  Require(
    structured_polled_result.Value().error_domain ==
      "UltrasonicDistanceService.GetDistanceStatistics",
    "structured queued method error domain changed");
  Require(
    !registry
       .SubmitMethodCallFuture({
         .service = service,
         .method_name = "ResetCalibration",
         .payload = {},
         .expects_response = false,
       })
       .HasValue(),
    "fire-and-forget method accepted a future call");

  const auto supplied_correlation_call = registry.SubmitMethodCall({
    .service = service,
    .method_name = "ResetCalibration",
    .payload = {},
    .correlation_id = 0x1234U,
  });
  Require(supplied_correlation_call.HasValue(), "supplied method correlation was rejected");
  Require(
    !registry
       .SubmitMethodCall({
         .service = service,
         .method_name = "ResetCalibration",
         .payload = {},
         .correlation_id = 0x1234U,
       })
       .HasValue(),
    "duplicate active method correlation was accepted");
  Require(
    registry.TakeMethodCall(service, "ResetCalibration").HasValue(),
    "supplied-correlation method call was not delivered");
  const auto application_error_result = registry.CompleteMethodCall({
    .service = service,
    .method_name = "ResetCalibration",
    .payload = {},
    .correlation_id = 0x1234U,
    .application_error = true,
    .error_domain = {},
    .error_code = 0U,
  });
  Require(application_error_result.HasValue(), "application-error method result was rejected");
  Require(
    !registry
       .SubmitMethodCall({
         .service = service,
         .method_name = "ResetCalibration",
         .payload = {},
         .correlation_id = 0x1234U,
       })
       .HasValue(),
    "method correlation with pending result was reused");
  const auto application_error = registry.TakeMethodResult(service, "ResetCalibration", 0x1234U);
  Require(application_error.HasValue(), "application-error method result was not delivered");
  Require(application_error.Value().application_error, "method application error flag changed");

  const auto fire_and_forget_call = registry.SubmitMethodCall({
    .service = service,
    .method_name = "ResetCalibration",
    .payload = {0x01U},
    .correlation_id = 0x2345U,
    .expects_response = false,
  });
  Require(fire_and_forget_call.HasValue(), "fire-and-forget method call was rejected");
  Require(
    !registry
       .SubmitMethodCall({
         .service = service,
         .method_name = "ResetCalibration",
         .payload = {0x02U},
         .correlation_id = 0x2345U,
         .expects_response = false,
       })
       .HasValue(),
    "duplicate queued fire-and-forget method correlation was accepted");
  const auto taken_fire_and_forget = registry.TakeMethodCall(service, "ResetCalibration");
  Require(taken_fire_and_forget.HasValue(), "fire-and-forget method call was not delivered");
  Require(
    !registry
       .CompleteMethodCall({
         .service = service,
         .method_name = "ResetCalibration",
         .payload = {},
         .correlation_id = 0x2345U,
         .application_error = false,
         .error_domain = {},
         .error_code = 0U,
       })
       .HasValue(),
    "fire-and-forget method call created an in-flight completion");
  Require(
    registry
      .SubmitMethodCall({
        .service = service,
        .method_name = "ResetCalibration",
        .payload = {0x03U},
        .correlation_id = 0x2345U,
        .expects_response = false,
      })
      .HasValue(),
    "delivered fire-and-forget correlation was not reusable");
  Require(
    registry.TakeMethodCall(service, "ResetCalibration").HasValue(),
    "reused fire-and-forget method call was not delivered");

  const auto stale_method_call = registry.SubmitMethodCall({
    .service = service,
    .method_name = "StaleMethod",
    .payload = {},
  });
  Require(stale_method_call.HasValue(), "stale-method setup call failed");
  const auto cancelled_future_call = registry.SubmitMethodCallFuture({
    .service = service,
    .method_name = "StaleFutureMethod",
    .payload = {},
  });
  Require(cancelled_future_call.HasValue(), "stale future setup call failed");
  const auto cancelled_future = cancelled_future_call.Value().future;
  const auto stale_trigger_subscription =
    registry.SubscribeTrigger(service, "ObstacleCleared", 1U);
  Require(stale_trigger_subscription.HasValue(), "stale trigger subscription setup failed");
  Require(
    registry
      .SetField({
        .service = service,
        .field_name = "CalibrationMode",
        .payload = {0x03U},
      })
      .HasValue(),
    "stale-field setup failed");

  registry.InvalidateEndpoint(service);
  Require(registry.FindService(service).empty(), "invalidated endpoint remained discoverable");
  Require(
    !registry.TakeMethodCall(service, "StaleMethod").HasValue(),
    "invalidated endpoint kept stale method calls");
  Require(cancelled_future.IsReady(), "invalidated endpoint left a future pending");
  Require(
    !cancelled_future.Get().HasValue(),
    "invalidated endpoint completed a cancelled future");
  Require(
    !registry.GetField(service, "CalibrationMode").HasValue(),
    "invalidated endpoint kept stale field value");
  Require(
    !registry.PollTrigger(stale_trigger_subscription.Value().id).HasValue(),
    "invalidated endpoint kept stale trigger queue");
  const auto publish_after_invalidate = registry.Publish({
    .service = service,
    .event_name = "DistanceSample",
    .payload = {4U},
  });
  Require(!publish_after_invalidate.HasValue(), "publish after endpoint invalidation succeeded");

  const auto stopped_find = registry.StopFind(find_handle.Value().id);
  Require(stopped_find.HasValue(), "stop-find failed");

  const auto unsubscribed = registry.Unsubscribe(subscription.Value().id);
  Require(unsubscribed.HasValue(), "unsubscribe failed");

  return 0;
}
