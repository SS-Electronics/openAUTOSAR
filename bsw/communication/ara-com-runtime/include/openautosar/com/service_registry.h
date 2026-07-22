// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/com/service_types.h"
#include "openautosar/core/future.h"
#include "openautosar/core/result.h"
#include "openautosar/security/identity_access_manager.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::com {

struct FindHandle final {
  std::uint64_t id{0U};
  ServiceIdentifier service{};

  friend bool operator==(const FindHandle&, const FindHandle&) = default;
};

struct Subscription final {
  std::uint64_t id{0U};
  ServiceIdentifier service{};
  std::string event_name;
  std::size_t queue_depth{1U};

  friend bool operator==(const Subscription&, const Subscription&) = default;
};

struct EventSample final {
  ServiceIdentifier service{};
  std::string event_name;
  std::vector<std::uint8_t> payload;
  std::uint64_t sequence{0U};

  friend bool operator==(const EventSample&, const EventSample&) = default;
};

struct MethodCall final {
  ServiceIdentifier service{};
  std::string method_name;
  std::vector<std::uint8_t> payload;
  std::uint64_t correlation_id{0U};
  bool expects_response{true};

  friend bool operator==(const MethodCall&, const MethodCall&) = default;
};

struct MethodResult final {
  ServiceIdentifier service{};
  std::string method_name;
  std::vector<std::uint8_t> payload;
  std::uint64_t correlation_id{0U};
  bool application_error{false};
  std::string error_domain;
  std::uint32_t error_code{0U};

  friend bool operator==(const MethodResult&, const MethodResult&) = default;
};

struct MethodCallFuture final {
  MethodCall call{};
  core::Future<MethodResult> future{};
};

struct FieldValue final {
  ServiceIdentifier service{};
  std::string field_name;
  std::vector<std::uint8_t> payload;
  std::uint64_t sequence{0U};

  friend bool operator==(const FieldValue&, const FieldValue&) = default;
};

struct TriggerActivation final {
  ServiceIdentifier service{};
  std::string trigger_name;
  std::uint64_t sequence{0U};

  friend bool operator==(const TriggerActivation&, const TriggerActivation&) = default;
};

class ServiceRegistry final {
public:
  ServiceRegistry() = default;
  explicit ServiceRegistry(const security::AccessPolicyEngine& access_policy) noexcept;

  void SetAccessPolicy(const security::AccessPolicyEngine& access_policy) noexcept;
  void ClearAccessPolicy() noexcept;
  void SetSecurityEventCollector(security::SecurityEventCollector& collector) noexcept;
  void ClearSecurityEventCollector() noexcept;

  [[nodiscard]] core::Result<ServiceOffer> OfferService(ServiceOffer offer);
  [[nodiscard]] core::Result<ServiceOffer> OfferServiceAs(
    const security::Principal& principal,
    ServiceOffer offer);
  [[nodiscard]] core::Result<bool> StopOffer(const ServiceIdentifier& service);
  [[nodiscard]] std::vector<ServiceOffer> FindService(const ServiceIdentifier& service) const;
  [[nodiscard]] core::Result<std::vector<ServiceOffer>> FindServiceAs(
    const security::Principal& principal,
    const ServiceIdentifier& service) const;

  [[nodiscard]] core::Result<FindHandle> StartFind(const ServiceIdentifier& service);
  [[nodiscard]] core::Result<FindHandle> StartFindAs(
    const security::Principal& principal,
    const ServiceIdentifier& service);
  [[nodiscard]] core::Result<bool> StopFind(std::uint64_t handle_id);

  [[nodiscard]] core::Result<Subscription> Subscribe(
    const ServiceIdentifier& service,
    std::string event_name,
    std::size_t queue_depth);
  [[nodiscard]] core::Result<Subscription> SubscribeAs(
    const security::Principal& principal,
    const ServiceIdentifier& service,
    std::string event_name,
    std::size_t queue_depth);
  [[nodiscard]] core::Result<bool> Unsubscribe(std::uint64_t subscription_id);

  [[nodiscard]] core::Result<std::uint64_t> Publish(EventSample sample);
  [[nodiscard]] core::Result<std::uint64_t> PublishAs(
    const security::Principal& principal,
    EventSample sample);
  [[nodiscard]] core::Result<EventSample> Poll(std::uint64_t subscription_id);

  [[nodiscard]] core::Result<MethodCall> SubmitMethodCall(MethodCall call);
  [[nodiscard]] core::Result<MethodCall> SubmitMethodCallAs(
    const security::Principal& principal,
    MethodCall call);
  [[nodiscard]] core::Result<MethodCallFuture> SubmitMethodCallFuture(MethodCall call);
  [[nodiscard]] core::Result<MethodCallFuture> SubmitMethodCallFutureAs(
    const security::Principal& principal,
    MethodCall call);
  [[nodiscard]] core::Result<MethodCall> TakeMethodCall(
    const ServiceIdentifier& service,
    std::string method_name);
  [[nodiscard]] core::Result<MethodResult> CompleteMethodCall(MethodResult result);
  [[nodiscard]] core::Result<MethodResult> CompleteMethodCallAs(
    const security::Principal& principal,
    MethodResult result);
  [[nodiscard]] core::Result<MethodResult> TakeMethodResult(
    const ServiceIdentifier& service,
    std::string method_name,
    std::uint64_t correlation_id);

  [[nodiscard]] core::Result<FieldValue> SetField(FieldValue value);
  [[nodiscard]] core::Result<FieldValue> SetFieldAs(
    const security::Principal& principal,
    FieldValue value);
  [[nodiscard]] core::Result<FieldValue> GetField(
    const ServiceIdentifier& service,
    std::string field_name) const;
  [[nodiscard]] core::Result<FieldValue> GetFieldAs(
    const security::Principal& principal,
    const ServiceIdentifier& service,
    std::string field_name) const;
  [[nodiscard]] core::Result<Subscription> SubscribeField(
    const ServiceIdentifier& service,
    std::string field_name,
    std::size_t queue_depth);
  [[nodiscard]] core::Result<Subscription> SubscribeFieldAs(
    const security::Principal& principal,
    const ServiceIdentifier& service,
    std::string field_name,
    std::size_t queue_depth);
  [[nodiscard]] core::Result<FieldValue> PollField(std::uint64_t subscription_id);

  [[nodiscard]] core::Result<Subscription> SubscribeTrigger(
    const ServiceIdentifier& service,
    std::string trigger_name,
    std::size_t queue_depth);
  [[nodiscard]] core::Result<Subscription> SubscribeTriggerAs(
    const security::Principal& principal,
    const ServiceIdentifier& service,
    std::string trigger_name,
    std::size_t queue_depth);
  [[nodiscard]] core::Result<TriggerActivation> FireTrigger(TriggerActivation activation);
  [[nodiscard]] core::Result<TriggerActivation> FireTriggerAs(
    const security::Principal& principal,
    TriggerActivation activation);
  [[nodiscard]] core::Result<TriggerActivation> PollTrigger(
    std::uint64_t subscription_id);

  void InvalidateEndpoint(const ServiceIdentifier& service);

private:
  struct SubscriptionState final {
    Subscription subscription;
    std::deque<EventSample> queue;
  };

  struct FieldSubscriptionState final {
    Subscription subscription;
    std::deque<FieldValue> queue;
  };

  struct MethodFutureState final {
    MethodCall call;
    core::Promise<MethodResult> promise;
  };

  struct TriggerSubscriptionState final {
    Subscription subscription;
    std::deque<TriggerActivation> queue;
  };

  [[nodiscard]] core::Result<ServiceOffer> OfferServiceAuthorized(
    std::optional<security::Principal> principal,
    ServiceOffer offer);
  [[nodiscard]] core::Result<FindHandle> StartFindAuthorized(
    std::optional<security::Principal> principal,
    const ServiceIdentifier& service);
  [[nodiscard]] core::Result<Subscription> SubscribeAuthorized(
    std::optional<security::Principal> principal,
    const ServiceIdentifier& service,
    std::string event_name,
    std::size_t queue_depth);
  [[nodiscard]] core::Result<std::uint64_t> PublishAuthorized(
    std::optional<security::Principal> principal,
    EventSample sample);
  [[nodiscard]] core::Result<MethodCall> SubmitMethodCallAuthorized(
    std::optional<security::Principal> principal,
    MethodCall call);
  [[nodiscard]] core::Result<MethodCallFuture> SubmitMethodCallFutureAuthorized(
    std::optional<security::Principal> principal,
    MethodCall call);
  [[nodiscard]] core::Result<MethodResult> CompleteMethodCallAuthorized(
    std::optional<security::Principal> principal,
    MethodResult result);
  [[nodiscard]] core::Result<FieldValue> SetFieldAuthorized(
    std::optional<security::Principal> principal,
    FieldValue value);
  [[nodiscard]] core::Result<FieldValue> GetFieldAuthorized(
    std::optional<security::Principal> principal,
    const ServiceIdentifier& service,
    std::string field_name) const;
  [[nodiscard]] core::Result<Subscription> SubscribeFieldAuthorized(
    std::optional<security::Principal> principal,
    const ServiceIdentifier& service,
    std::string field_name,
    std::size_t queue_depth);
  [[nodiscard]] core::Result<Subscription> SubscribeTriggerAuthorized(
    std::optional<security::Principal> principal,
    const ServiceIdentifier& service,
    std::string trigger_name,
    std::size_t queue_depth);
  [[nodiscard]] core::Result<TriggerActivation> FireTriggerAuthorized(
    std::optional<security::Principal> principal,
    TriggerActivation activation);
  [[nodiscard]] core::Result<bool> AuthorizeServiceAccess(
    const std::optional<security::Principal>& principal,
    security::Operation operation,
    const ServiceIdentifier& service,
    std::string_view policy_id) const;

  [[nodiscard]] core::Result<std::string> ValidateOffer(const ServiceOffer& offer) const;
  [[nodiscard]] bool HasOfferedService(const ServiceIdentifier& service) const;
  [[nodiscard]] const ServiceOffer* OfferedService(const ServiceIdentifier& service) const;
  [[nodiscard]] static std::string KeyFor(const ServiceIdentifier& service);
  [[nodiscard]] static std::string MethodKeyFor(
    const ServiceIdentifier& service,
    std::string_view method_name);
  [[nodiscard]] static std::string FieldKeyFor(
    const ServiceIdentifier& service,
    std::string_view field_name);
  void EraseMethodQueuesFor(const ServiceIdentifier& service);
  void EraseFieldStateFor(const ServiceIdentifier& service);
  void EraseTriggerStateFor(const ServiceIdentifier& service);

  std::map<std::string, ServiceOffer> offers_;
  std::map<std::uint64_t, FindHandle> find_handles_;
  std::map<std::uint64_t, SubscriptionState> subscriptions_;
  std::map<std::uint64_t, FieldSubscriptionState> field_subscriptions_;
  std::map<std::uint64_t, TriggerSubscriptionState> trigger_subscriptions_;
  std::map<std::string, std::deque<MethodCall>> pending_method_calls_;
  std::map<std::uint64_t, MethodCall> in_flight_method_calls_;
  std::map<std::string, std::deque<MethodResult>> pending_method_results_;
  std::map<std::uint64_t, MethodFutureState> method_futures_;
  std::map<std::string, FieldValue> field_values_;
  const security::AccessPolicyEngine* access_policy_{nullptr};
  security::SecurityEventCollector* security_events_{nullptr};
  std::uint64_t next_handle_id_{1U};
  std::uint64_t next_subscription_id_{1U};
  std::uint64_t next_sequence_{1U};
  std::uint64_t next_method_correlation_id_{1U};
};

}  // namespace openautosar::com
