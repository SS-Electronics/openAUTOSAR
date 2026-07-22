// SPDX-License-Identifier: MIT

#include "openautosar/com/service_registry.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace openautosar::com {
namespace {

using MethodCallQueues = std::map<std::string, std::deque<MethodCall>>;
using MethodResultQueues = std::map<std::string, std::deque<MethodResult>>;

[[nodiscard]] bool QueuesContainCorrelation(
  const MethodCallQueues& queues,
  std::uint64_t correlation_id) {
  for (const auto& [_, queue] : queues) {
    const auto found = std::find_if(
      queue.begin(),
      queue.end(),
      [correlation_id](const MethodCall& call) {
        return call.correlation_id == correlation_id;
      });
    if (found != queue.end()) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool QueuesContainCorrelation(
  const MethodResultQueues& queues,
  std::uint64_t correlation_id) {
  for (const auto& [_, queue] : queues) {
    const auto found = std::find_if(
      queue.begin(),
      queue.end(),
      [correlation_id](const MethodResult& result) {
        return result.correlation_id == correlation_id;
      });
    if (found != queue.end()) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool CorrelationIsKnown(
  const MethodCallQueues& pending_calls,
  const std::map<std::uint64_t, MethodCall>& in_flight_calls,
  const MethodResultQueues& pending_results,
  std::uint64_t correlation_id) {
  return QueuesContainCorrelation(pending_calls, correlation_id) ||
         in_flight_calls.find(correlation_id) != in_flight_calls.end() ||
         QueuesContainCorrelation(pending_results, correlation_id);
}

}  // namespace

ServiceRegistry::ServiceRegistry(const security::AccessPolicyEngine& access_policy) noexcept
  : access_policy_(&access_policy) {}

void ServiceRegistry::SetAccessPolicy(
  const security::AccessPolicyEngine& access_policy) noexcept {
  access_policy_ = &access_policy;
}

void ServiceRegistry::ClearAccessPolicy() noexcept {
  access_policy_ = nullptr;
}

void ServiceRegistry::SetSecurityEventCollector(
  security::SecurityEventCollector& collector) noexcept {
  security_events_ = &collector;
}

void ServiceRegistry::ClearSecurityEventCollector() noexcept {
  security_events_ = nullptr;
}

core::Result<ServiceOffer> ServiceRegistry::OfferService(ServiceOffer offer) {
  return OfferServiceAuthorized(std::nullopt, std::move(offer));
}

core::Result<ServiceOffer> ServiceRegistry::OfferServiceAs(
  const security::Principal& principal,
  ServiceOffer offer) {
  return OfferServiceAuthorized(principal, std::move(offer));
}

core::Result<ServiceOffer> ServiceRegistry::OfferServiceAuthorized(
  std::optional<security::Principal> principal,
  ServiceOffer offer) {
  auto validation = ValidateOffer(offer);
  if (!validation) {
    return core::Result<ServiceOffer>::FromError(validation.Error());
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kOfferService,
    offer.service,
    offer.access_policy);
  if (!authorized) {
    return core::Result<ServiceOffer>::FromError(authorized.Error());
  }

  offer.offer_state = OfferState::kOffered;
  const auto key = KeyFor(offer.service);
  offers_[key] = offer;
  return core::Result<ServiceOffer>::FromValue(std::move(offer));
}

core::Result<bool> ServiceRegistry::StopOffer(const ServiceIdentifier& service) {
  const auto key = KeyFor(service);
  auto iter = offers_.find(key);
  if (iter == offers_.end()) {
    return core::Result<bool>::FromError({"ara-com-runtime", "service is not offered"});
  }

  iter->second.offer_state = OfferState::kStopped;
  EraseMethodQueuesFor(service);
  EraseFieldStateFor(service);
  EraseTriggerStateFor(service);
  return core::Result<bool>::FromValue(true);
}

std::vector<ServiceOffer> ServiceRegistry::FindService(const ServiceIdentifier& service) const {
  std::vector<ServiceOffer> result;
  const auto key = KeyFor(service);
  const auto iter = offers_.find(key);
  if (iter != offers_.end() && iter->second.offer_state == OfferState::kOffered) {
    result.push_back(iter->second);
  }
  return result;
}

core::Result<std::vector<ServiceOffer>> ServiceRegistry::FindServiceAs(
  const security::Principal& principal,
  const ServiceIdentifier& service) const {
  const auto offer = OfferedService(service);
  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kFindService,
    service,
    offer == nullptr ? std::string_view{} : std::string_view{offer->access_policy});
  if (!authorized) {
    return core::Result<std::vector<ServiceOffer>>::FromError(authorized.Error());
  }

  return core::Result<std::vector<ServiceOffer>>::FromValue(FindService(service));
}

core::Result<FindHandle> ServiceRegistry::StartFind(const ServiceIdentifier& service) {
  return StartFindAuthorized(std::nullopt, service);
}

core::Result<FindHandle> ServiceRegistry::StartFindAs(
  const security::Principal& principal,
  const ServiceIdentifier& service) {
  return StartFindAuthorized(principal, service);
}

core::Result<FindHandle> ServiceRegistry::StartFindAuthorized(
  std::optional<security::Principal> principal,
  const ServiceIdentifier& service) {
  if (service.interface_id == 0U || service.instance_id == 0U) {
    return core::Result<FindHandle>::FromError(
      {"ara-com-runtime", "find request has invalid service identifier"});
  }

  const auto offer = OfferedService(service);
  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kFindService,
    service,
    offer == nullptr ? std::string_view{} : std::string_view{offer->access_policy});
  if (!authorized) {
    return core::Result<FindHandle>::FromError(authorized.Error());
  }

  FindHandle handle{.id = next_handle_id_++, .service = service};
  find_handles_[handle.id] = handle;
  return core::Result<FindHandle>::FromValue(handle);
}

core::Result<bool> ServiceRegistry::StopFind(std::uint64_t handle_id) {
  const auto erased = find_handles_.erase(handle_id);
  if (erased == 0U) {
    return core::Result<bool>::FromError({"ara-com-runtime", "find handle is not active"});
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<Subscription> ServiceRegistry::Subscribe(
  const ServiceIdentifier& service,
  std::string event_name,
  std::size_t queue_depth) {
  return SubscribeAuthorized(std::nullopt, service, std::move(event_name), queue_depth);
}

core::Result<Subscription> ServiceRegistry::SubscribeAs(
  const security::Principal& principal,
  const ServiceIdentifier& service,
  std::string event_name,
  std::size_t queue_depth) {
  return SubscribeAuthorized(principal, service, std::move(event_name), queue_depth);
}

core::Result<Subscription> ServiceRegistry::SubscribeAuthorized(
  std::optional<security::Principal> principal,
  const ServiceIdentifier& service,
  std::string event_name,
  std::size_t queue_depth) {
  const auto offer = OfferedService(service);
  if (offer == nullptr) {
    return core::Result<Subscription>::FromError(
      {"ara-com-runtime", "cannot subscribe to a service that is not offered"});
  }

  if (event_name.empty()) {
    return core::Result<Subscription>::FromError(
      {"ara-com-runtime", "event name is empty"});
  }

  if (queue_depth == 0U || queue_depth > 64U) {
    return core::Result<Subscription>::FromError(
      {"ara-com-runtime", "event queue depth is outside supported bounds"});
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kSubscribeEvent,
    service,
    offer->access_policy);
  if (!authorized) {
    return core::Result<Subscription>::FromError(authorized.Error());
  }

  Subscription subscription{
    .id = next_subscription_id_++,
    .service = service,
    .event_name = std::move(event_name),
    .queue_depth = queue_depth,
  };
  subscriptions_.emplace(subscription.id, SubscriptionState{subscription, {}});
  return core::Result<Subscription>::FromValue(subscription);
}

core::Result<bool> ServiceRegistry::Unsubscribe(std::uint64_t subscription_id) {
  const auto erased_events = subscriptions_.erase(subscription_id);
  const auto erased_fields = field_subscriptions_.erase(subscription_id);
  const auto erased_triggers = trigger_subscriptions_.erase(subscription_id);
  if (erased_events == 0U && erased_fields == 0U && erased_triggers == 0U) {
    return core::Result<bool>::FromError({"ara-com-runtime", "subscription is not active"});
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<std::uint64_t> ServiceRegistry::Publish(EventSample sample) {
  return PublishAuthorized(std::nullopt, std::move(sample));
}

core::Result<std::uint64_t> ServiceRegistry::PublishAs(
  const security::Principal& principal,
  EventSample sample) {
  return PublishAuthorized(principal, std::move(sample));
}

core::Result<std::uint64_t> ServiceRegistry::PublishAuthorized(
  std::optional<security::Principal> principal,
  EventSample sample) {
  const auto offer = OfferedService(sample.service);
  if (offer == nullptr) {
    return core::Result<std::uint64_t>::FromError(
      {"ara-com-runtime", "cannot publish from a service that is not offered"});
  }

  if (sample.event_name.empty()) {
    return core::Result<std::uint64_t>::FromError({"ara-com-runtime", "event name is empty"});
  }

  if (sample.payload.empty()) {
    return core::Result<std::uint64_t>::FromError({"ara-com-runtime", "event payload is empty"});
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kPublishEvent,
    sample.service,
    offer->access_policy);
  if (!authorized) {
    return core::Result<std::uint64_t>::FromError(authorized.Error());
  }

  sample.sequence = next_sequence_++;

  for (auto& [_, state] : subscriptions_) {
    if (state.subscription.service == sample.service &&
        state.subscription.event_name == sample.event_name) {
      state.queue.push_back(sample);
      while (state.queue.size() > state.subscription.queue_depth) {
        state.queue.pop_front();
      }
    }
  }

  return core::Result<std::uint64_t>::FromValue(sample.sequence);
}

core::Result<EventSample> ServiceRegistry::Poll(std::uint64_t subscription_id) {
  auto iter = subscriptions_.find(subscription_id);
  if (iter == subscriptions_.end()) {
    return core::Result<EventSample>::FromError({"ara-com-runtime", "subscription is not active"});
  }

  auto& queue = iter->second.queue;
  if (queue.empty()) {
    return core::Result<EventSample>::FromError({"ara-com-runtime", "event queue is empty"});
  }

  auto sample = queue.front();
  queue.pop_front();
  return core::Result<EventSample>::FromValue(std::move(sample));
}

core::Result<MethodCall> ServiceRegistry::SubmitMethodCall(MethodCall call) {
  return SubmitMethodCallAuthorized(std::nullopt, std::move(call));
}

core::Result<MethodCall> ServiceRegistry::SubmitMethodCallAs(
  const security::Principal& principal,
  MethodCall call) {
  return SubmitMethodCallAuthorized(principal, std::move(call));
}

core::Result<MethodCallFuture> ServiceRegistry::SubmitMethodCallFuture(MethodCall call) {
  return SubmitMethodCallFutureAuthorized(std::nullopt, std::move(call));
}

core::Result<MethodCallFuture> ServiceRegistry::SubmitMethodCallFutureAs(
  const security::Principal& principal,
  MethodCall call) {
  return SubmitMethodCallFutureAuthorized(principal, std::move(call));
}

core::Result<MethodCall> ServiceRegistry::SubmitMethodCallAuthorized(
  std::optional<security::Principal> principal,
  MethodCall call) {
  const auto offer = OfferedService(call.service);
  if (offer == nullptr) {
    return core::Result<MethodCall>::FromError(
      {"ara-com-runtime", "cannot call a service that is not offered"});
  }

  if (call.method_name.empty()) {
    return core::Result<MethodCall>::FromError(
      {"ara-com-runtime", "method name is empty"});
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kCallMethod,
    call.service,
    offer->access_policy);
  if (!authorized) {
    return core::Result<MethodCall>::FromError(authorized.Error());
  }

  if (call.correlation_id == 0U) {
    do {
      call.correlation_id = next_method_correlation_id_++;
      if (next_method_correlation_id_ == 0U) {
        next_method_correlation_id_ = 1U;
      }
    } while (CorrelationIsKnown(
      pending_method_calls_,
      in_flight_method_calls_,
      pending_method_results_,
      call.correlation_id));
  } else if (CorrelationIsKnown(
               pending_method_calls_,
               in_flight_method_calls_,
               pending_method_results_,
               call.correlation_id)) {
    return core::Result<MethodCall>::FromError(
      {"ara-com-runtime", "method correlation id is already active"});
  }

  pending_method_calls_[MethodKeyFor(call.service, call.method_name)].push_back(call);
  return core::Result<MethodCall>::FromValue(std::move(call));
}

core::Result<MethodCallFuture> ServiceRegistry::SubmitMethodCallFutureAuthorized(
  std::optional<security::Principal> principal,
  MethodCall call) {
  if (!call.expects_response) {
    return core::Result<MethodCallFuture>::FromError(
      {"ara-com-runtime", "future method call cannot be fire-and-forget"});
  }

  auto submitted = SubmitMethodCallAuthorized(principal, std::move(call));
  if (!submitted) {
    return core::Result<MethodCallFuture>::FromError(submitted.Error());
  }

  core::Promise<MethodResult> promise;
  auto future = promise.GetFuture();
  MethodCall submitted_call = submitted.Value();
  method_futures_.emplace(
    submitted_call.correlation_id,
    MethodFutureState{
      .call = submitted_call,
      .promise = std::move(promise),
    });

  return core::Result<MethodCallFuture>::FromValue({
    .call = std::move(submitted_call),
    .future = std::move(future),
  });
}

core::Result<MethodCall> ServiceRegistry::TakeMethodCall(
  const ServiceIdentifier& service,
  std::string method_name) {
  if (OfferedService(service) == nullptr) {
    return core::Result<MethodCall>::FromError(
      {"ara-com-runtime", "cannot take a call for a service that is not offered"});
  }

  if (method_name.empty()) {
    return core::Result<MethodCall>::FromError({"ara-com-runtime", "method name is empty"});
  }

  const auto key = MethodKeyFor(service, method_name);
  auto queue = pending_method_calls_.find(key);
  if (queue == pending_method_calls_.end() || queue->second.empty()) {
    return core::Result<MethodCall>::FromError(
      {"ara-com-runtime", "method call queue is empty"});
  }

  auto call = queue->second.front();
  queue->second.pop_front();
  if (queue->second.empty()) {
    pending_method_calls_.erase(queue);
  }

  if (call.expects_response) {
    in_flight_method_calls_[call.correlation_id] = call;
  }
  return core::Result<MethodCall>::FromValue(std::move(call));
}

core::Result<MethodResult> ServiceRegistry::CompleteMethodCall(MethodResult result) {
  return CompleteMethodCallAuthorized(std::nullopt, std::move(result));
}

core::Result<MethodResult> ServiceRegistry::CompleteMethodCallAs(
  const security::Principal& principal,
  MethodResult result) {
  return CompleteMethodCallAuthorized(principal, std::move(result));
}

core::Result<MethodResult> ServiceRegistry::CompleteMethodCallAuthorized(
  std::optional<security::Principal> principal,
  MethodResult result) {
  const auto offer = OfferedService(result.service);
  if (offer == nullptr) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "cannot complete a call for a service that is not offered"});
  }

  if (result.method_name.empty()) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "method name is empty"});
  }

  if (result.correlation_id == 0U) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "method result correlation id is zero"});
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kCompleteMethod,
    result.service,
    offer->access_policy);
  if (!authorized) {
    return core::Result<MethodResult>::FromError(authorized.Error());
  }

  auto in_flight = in_flight_method_calls_.find(result.correlation_id);
  if (in_flight == in_flight_method_calls_.end()) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "method result does not match an in-flight call"});
  }

  if (in_flight->second.service != result.service ||
      in_flight->second.method_name != result.method_name) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "method result does not match the in-flight service method"});
  }

  in_flight_method_calls_.erase(in_flight);
  pending_method_results_[MethodKeyFor(result.service, result.method_name)].push_back(result);
  auto future = method_futures_.find(result.correlation_id);
  if (future != method_futures_.end()) {
    static_cast<void>(future->second.promise.SetValue(result));
    method_futures_.erase(future);
  }
  return core::Result<MethodResult>::FromValue(std::move(result));
}

core::Result<MethodResult> ServiceRegistry::TakeMethodResult(
  const ServiceIdentifier& service,
  std::string method_name,
  std::uint64_t correlation_id) {
  if (service.interface_id == 0U || service.instance_id == 0U) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "method result service identifier is invalid"});
  }

  if (method_name.empty()) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "method name is empty"});
  }

  if (correlation_id == 0U) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "method result correlation id is zero"});
  }

  const auto key = MethodKeyFor(service, method_name);
  auto queue = pending_method_results_.find(key);
  if (queue == pending_method_results_.end()) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "method result queue is empty"});
  }

  auto result = std::find_if(
    queue->second.begin(),
    queue->second.end(),
    [correlation_id](const MethodResult& candidate) {
      return candidate.correlation_id == correlation_id;
    });
  if (result == queue->second.end()) {
    return core::Result<MethodResult>::FromError(
      {"ara-com-runtime", "method result correlation id is not available"});
  }

  auto value = std::move(*result);
  queue->second.erase(result);
  if (queue->second.empty()) {
    pending_method_results_.erase(queue);
  }

  return core::Result<MethodResult>::FromValue(std::move(value));
}

core::Result<FieldValue> ServiceRegistry::SetField(FieldValue value) {
  return SetFieldAuthorized(std::nullopt, std::move(value));
}

core::Result<FieldValue> ServiceRegistry::SetFieldAs(
  const security::Principal& principal,
  FieldValue value) {
  return SetFieldAuthorized(principal, std::move(value));
}

core::Result<FieldValue> ServiceRegistry::SetFieldAuthorized(
  std::optional<security::Principal> principal,
  FieldValue value) {
  const auto offer = OfferedService(value.service);
  if (offer == nullptr) {
    return core::Result<FieldValue>::FromError(
      {"ara-com-runtime", "cannot set a field for a service that is not offered"});
  }

  if (value.field_name.empty()) {
    return core::Result<FieldValue>::FromError({"ara-com-runtime", "field name is empty"});
  }

  if (value.payload.empty()) {
    return core::Result<FieldValue>::FromError({"ara-com-runtime", "field payload is empty"});
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kSetField,
    value.service,
    offer->access_policy);
  if (!authorized) {
    return core::Result<FieldValue>::FromError(authorized.Error());
  }

  value.sequence = next_sequence_++;
  field_values_[FieldKeyFor(value.service, value.field_name)] = value;

  for (auto& [_, state] : field_subscriptions_) {
    if (state.subscription.service == value.service &&
        state.subscription.event_name == value.field_name) {
      state.queue.push_back(value);
      while (state.queue.size() > state.subscription.queue_depth) {
        state.queue.pop_front();
      }
    }
  }

  return core::Result<FieldValue>::FromValue(std::move(value));
}

core::Result<FieldValue> ServiceRegistry::GetField(
  const ServiceIdentifier& service,
  std::string field_name) const {
  return GetFieldAuthorized(std::nullopt, service, std::move(field_name));
}

core::Result<FieldValue> ServiceRegistry::GetFieldAs(
  const security::Principal& principal,
  const ServiceIdentifier& service,
  std::string field_name) const {
  return GetFieldAuthorized(principal, service, std::move(field_name));
}

core::Result<FieldValue> ServiceRegistry::GetFieldAuthorized(
  std::optional<security::Principal> principal,
  const ServiceIdentifier& service,
  std::string field_name) const {
  const auto offer = OfferedService(service);
  if (offer == nullptr) {
    return core::Result<FieldValue>::FromError(
      {"ara-com-runtime", "cannot get a field for a service that is not offered"});
  }

  if (field_name.empty()) {
    return core::Result<FieldValue>::FromError({"ara-com-runtime", "field name is empty"});
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kGetField,
    service,
    offer->access_policy);
  if (!authorized) {
    return core::Result<FieldValue>::FromError(authorized.Error());
  }

  const auto value = field_values_.find(FieldKeyFor(service, field_name));
  if (value == field_values_.end()) {
    return core::Result<FieldValue>::FromError({"ara-com-runtime", "field value is not available"});
  }

  return core::Result<FieldValue>::FromValue(value->second);
}

core::Result<Subscription> ServiceRegistry::SubscribeField(
  const ServiceIdentifier& service,
  std::string field_name,
  std::size_t queue_depth) {
  return SubscribeFieldAuthorized(std::nullopt, service, std::move(field_name), queue_depth);
}

core::Result<Subscription> ServiceRegistry::SubscribeFieldAs(
  const security::Principal& principal,
  const ServiceIdentifier& service,
  std::string field_name,
  std::size_t queue_depth) {
  return SubscribeFieldAuthorized(principal, service, std::move(field_name), queue_depth);
}

core::Result<Subscription> ServiceRegistry::SubscribeFieldAuthorized(
  std::optional<security::Principal> principal,
  const ServiceIdentifier& service,
  std::string field_name,
  std::size_t queue_depth) {
  const auto offer = OfferedService(service);
  if (offer == nullptr) {
    return core::Result<Subscription>::FromError(
      {"ara-com-runtime", "cannot subscribe to a field for a service that is not offered"});
  }

  if (field_name.empty()) {
    return core::Result<Subscription>::FromError({"ara-com-runtime", "field name is empty"});
  }

  if (queue_depth == 0U || queue_depth > 64U) {
    return core::Result<Subscription>::FromError(
      {"ara-com-runtime", "field queue depth is outside supported bounds"});
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kSubscribeField,
    service,
    offer->access_policy);
  if (!authorized) {
    return core::Result<Subscription>::FromError(authorized.Error());
  }

  Subscription subscription{
    .id = next_subscription_id_++,
    .service = service,
    .event_name = std::move(field_name),
    .queue_depth = queue_depth,
  };
  FieldSubscriptionState state{subscription, {}};
  const auto current_value = field_values_.find(
    FieldKeyFor(subscription.service, subscription.event_name));
  if (current_value != field_values_.end()) {
    state.queue.push_back(current_value->second);
  }

  field_subscriptions_.emplace(subscription.id, std::move(state));
  return core::Result<Subscription>::FromValue(subscription);
}

core::Result<FieldValue> ServiceRegistry::PollField(std::uint64_t subscription_id) {
  auto iter = field_subscriptions_.find(subscription_id);
  if (iter == field_subscriptions_.end()) {
    return core::Result<FieldValue>::FromError(
      {"ara-com-runtime", "field subscription is not active"});
  }

  auto& queue = iter->second.queue;
  if (queue.empty()) {
    return core::Result<FieldValue>::FromError({"ara-com-runtime", "field queue is empty"});
  }

  auto value = queue.front();
  queue.pop_front();
  return core::Result<FieldValue>::FromValue(std::move(value));
}

core::Result<Subscription> ServiceRegistry::SubscribeTrigger(
  const ServiceIdentifier& service,
  std::string trigger_name,
  std::size_t queue_depth) {
  return SubscribeTriggerAuthorized(
    std::nullopt,
    service,
    std::move(trigger_name),
    queue_depth);
}

core::Result<Subscription> ServiceRegistry::SubscribeTriggerAs(
  const security::Principal& principal,
  const ServiceIdentifier& service,
  std::string trigger_name,
  std::size_t queue_depth) {
  return SubscribeTriggerAuthorized(
    principal,
    service,
    std::move(trigger_name),
    queue_depth);
}

core::Result<Subscription> ServiceRegistry::SubscribeTriggerAuthorized(
  std::optional<security::Principal> principal,
  const ServiceIdentifier& service,
  std::string trigger_name,
  std::size_t queue_depth) {
  const auto offer = OfferedService(service);
  if (offer == nullptr) {
    return core::Result<Subscription>::FromError(
      {"ara-com-runtime", "cannot subscribe to a trigger for a service that is not offered"});
  }

  if (trigger_name.empty()) {
    return core::Result<Subscription>::FromError({"ara-com-runtime", "trigger name is empty"});
  }

  if (queue_depth == 0U || queue_depth > 64U) {
    return core::Result<Subscription>::FromError(
      {"ara-com-runtime", "trigger queue depth is outside supported bounds"});
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kSubscribeTrigger,
    service,
    offer->access_policy);
  if (!authorized) {
    return core::Result<Subscription>::FromError(authorized.Error());
  }

  Subscription subscription{
    .id = next_subscription_id_++,
    .service = service,
    .event_name = std::move(trigger_name),
    .queue_depth = queue_depth,
  };
  trigger_subscriptions_.emplace(
    subscription.id,
    TriggerSubscriptionState{subscription, {}});
  return core::Result<Subscription>::FromValue(subscription);
}

core::Result<TriggerActivation> ServiceRegistry::FireTrigger(
  TriggerActivation activation) {
  return FireTriggerAuthorized(std::nullopt, std::move(activation));
}

core::Result<TriggerActivation> ServiceRegistry::FireTriggerAs(
  const security::Principal& principal,
  TriggerActivation activation) {
  return FireTriggerAuthorized(principal, std::move(activation));
}

core::Result<TriggerActivation> ServiceRegistry::FireTriggerAuthorized(
  std::optional<security::Principal> principal,
  TriggerActivation activation) {
  const auto offer = OfferedService(activation.service);
  if (offer == nullptr) {
    return core::Result<TriggerActivation>::FromError(
      {"ara-com-runtime", "cannot fire a trigger for a service that is not offered"});
  }

  if (activation.trigger_name.empty()) {
    return core::Result<TriggerActivation>::FromError(
      {"ara-com-runtime", "trigger name is empty"});
  }

  auto authorized = AuthorizeServiceAccess(
    principal,
    security::Operation::kFireTrigger,
    activation.service,
    offer->access_policy);
  if (!authorized) {
    return core::Result<TriggerActivation>::FromError(authorized.Error());
  }

  activation.sequence = next_sequence_++;
  for (auto& [_, state] : trigger_subscriptions_) {
    if (state.subscription.service == activation.service &&
        state.subscription.event_name == activation.trigger_name) {
      state.queue.push_back(activation);
      while (state.queue.size() > state.subscription.queue_depth) {
        state.queue.pop_front();
      }
    }
  }

  return core::Result<TriggerActivation>::FromValue(std::move(activation));
}

core::Result<TriggerActivation> ServiceRegistry::PollTrigger(
  std::uint64_t subscription_id) {
  auto iter = trigger_subscriptions_.find(subscription_id);
  if (iter == trigger_subscriptions_.end()) {
    return core::Result<TriggerActivation>::FromError(
      {"ara-com-runtime", "trigger subscription is not active"});
  }

  auto& queue = iter->second.queue;
  if (queue.empty()) {
    return core::Result<TriggerActivation>::FromError(
      {"ara-com-runtime", "trigger queue is empty"});
  }

  auto activation = queue.front();
  queue.pop_front();
  return core::Result<TriggerActivation>::FromValue(std::move(activation));
}

void ServiceRegistry::InvalidateEndpoint(const ServiceIdentifier& service) {
  const auto key = KeyFor(service);
  auto iter = offers_.find(key);
  if (iter == offers_.end()) {
    return;
  }

  iter->second.offer_state = OfferState::kStopped;
  iter->second.health_state = HealthState::kFailed;
  EraseMethodQueuesFor(service);
  EraseFieldStateFor(service);
  EraseTriggerStateFor(service);
}

core::Result<std::string> ServiceRegistry::ValidateOffer(const ServiceOffer& offer) const {
  if (offer.service.interface_id == 0U || offer.service.instance_id == 0U) {
    return core::Result<std::string>::FromError(
      {"ara-com-runtime", "service offer has invalid service identifier"});
  }

  if (offer.service.major_version == 0U) {
    return core::Result<std::string>::FromError(
      {"ara-com-runtime", "service major version must be non-zero"});
  }

  if (offer.process_identity.empty() || offer.machine_identity.empty()) {
    return core::Result<std::string>::FromError(
      {"ara-com-runtime", "service offer is missing process or machine identity"});
  }

  if (offer.endpoint.address.empty()) {
    return core::Result<std::string>::FromError(
      {"ara-com-runtime", "service endpoint address is empty"});
  }

  if (offer.ttl_ms == 0U) {
    return core::Result<std::string>::FromError({"ara-com-runtime", "service ttl is zero"});
  }

  return core::Result<std::string>::FromValue(KeyFor(offer.service));
}

core::Result<bool> ServiceRegistry::AuthorizeServiceAccess(
  const std::optional<security::Principal>& principal,
  security::Operation operation,
  const ServiceIdentifier& service,
  std::string_view policy_id) const {
  if (access_policy_ == nullptr) {
    return core::Result<bool>::FromValue(true);
  }

  if (!principal.has_value()) {
    if (security_events_ != nullptr) {
      static_cast<void>(security_events_->Record({
        .source = "ara-com",
        .category = "authorization",
        .severity = security::SecuritySeverity::kCritical,
        .principal_id = "anonymous",
        .resource_id = KeyFor(service),
        .operation = operation,
        .detail = "security principal is required by access policy",
      }));
    }
    return core::Result<bool>::FromError(
      {"ara-com-runtime", "security principal is required by access policy"});
  }

  const auto decision = access_policy_->Authorize({
    .principal = principal.value(),
    .resource = {
      .kind = security::ResourceKind::kService,
      .identifier = KeyFor(service),
      .policy_id = std::string(policy_id),
    },
    .operation = operation,
    .action = std::string(security::ToString(operation)),
  });
  if (!decision.Allowed()) {
    if (security_events_ != nullptr) {
      static_cast<void>(security_events_->Record({
        .source = "ara-com",
        .category = "authorization",
        .severity = decision.severity,
        .principal_id = principal->application_id,
        .resource_id = KeyFor(service),
        .operation = operation,
        .detail = decision.reason,
      }));
    }
    return core::Result<bool>::FromError({"ara-com-runtime", decision.reason});
  }

  return core::Result<bool>::FromValue(true);
}

bool ServiceRegistry::HasOfferedService(const ServiceIdentifier& service) const {
  const auto offers = FindService(service);
  return !offers.empty();
}

const ServiceOffer* ServiceRegistry::OfferedService(const ServiceIdentifier& service) const {
  const auto key = KeyFor(service);
  const auto iter = offers_.find(key);
  if (iter == offers_.end() || iter->second.offer_state != OfferState::kOffered) {
    return nullptr;
  }

  return &iter->second;
}

std::string ServiceRegistry::KeyFor(const ServiceIdentifier& service) {
  std::ostringstream stream;
  stream << service.interface_id << ':' << service.instance_id << ':'
         << service.major_version << ':' << service.minor_version;
  return stream.str();
}

std::string ServiceRegistry::MethodKeyFor(
  const ServiceIdentifier& service,
  std::string_view method_name) {
  return KeyFor(service) + ':' + std::string(method_name);
}

std::string ServiceRegistry::FieldKeyFor(
  const ServiceIdentifier& service,
  std::string_view field_name) {
  return KeyFor(service) + ':' + std::string(field_name);
}

void ServiceRegistry::EraseMethodQueuesFor(const ServiceIdentifier& service) {
  const auto prefix = KeyFor(service) + ':';
  for (auto iter = pending_method_calls_.begin(); iter != pending_method_calls_.end();) {
    if (iter->first.rfind(prefix, 0U) == 0U) {
      iter = pending_method_calls_.erase(iter);
    } else {
      ++iter;
    }
  }

  for (auto iter = pending_method_results_.begin(); iter != pending_method_results_.end();) {
    if (iter->first.rfind(prefix, 0U) == 0U) {
      iter = pending_method_results_.erase(iter);
    } else {
      ++iter;
    }
  }

  for (auto iter = in_flight_method_calls_.begin(); iter != in_flight_method_calls_.end();) {
    if (iter->second.service == service) {
      iter = in_flight_method_calls_.erase(iter);
    } else {
      ++iter;
    }
  }

  for (auto iter = method_futures_.begin(); iter != method_futures_.end();) {
    if (iter->second.call.service == service) {
      static_cast<void>(iter->second.promise.SetError({
        "ara-com-runtime",
        "method future was cancelled because the service stopped",
      }));
      iter = method_futures_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void ServiceRegistry::EraseFieldStateFor(const ServiceIdentifier& service) {
  const auto prefix = KeyFor(service) + ':';
  for (auto iter = field_values_.begin(); iter != field_values_.end();) {
    if (iter->first.rfind(prefix, 0U) == 0U) {
      iter = field_values_.erase(iter);
    } else {
      ++iter;
    }
  }

  for (auto iter = field_subscriptions_.begin(); iter != field_subscriptions_.end();) {
    if (iter->second.subscription.service == service) {
      iter = field_subscriptions_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void ServiceRegistry::EraseTriggerStateFor(const ServiceIdentifier& service) {
  for (auto iter = trigger_subscriptions_.begin(); iter != trigger_subscriptions_.end();) {
    if (iter->second.subscription.service == service) {
      iter = trigger_subscriptions_.erase(iter);
    } else {
      ++iter;
    }
  }
}

}  // namespace openautosar::com
