// SPDX-License-Identifier: MIT

#include "openautosar/com/service_registry.h"

#include <sstream>
#include <utility>

namespace openautosar::com {

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
  const auto erased = subscriptions_.erase(subscription_id);
  if (erased == 0U) {
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

void ServiceRegistry::InvalidateEndpoint(const ServiceIdentifier& service) {
  const auto key = KeyFor(service);
  auto iter = offers_.find(key);
  if (iter == offers_.end()) {
    return;
  }

  iter->second.offer_state = OfferState::kStopped;
  iter->second.health_state = HealthState::kFailed;
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

}  // namespace openautosar::com
