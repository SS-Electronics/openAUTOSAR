// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/com/service_types.h"
#include "openautosar/core/result.h"
#include "openautosar/security/identity_access_manager.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
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

  void InvalidateEndpoint(const ServiceIdentifier& service);

private:
  struct SubscriptionState final {
    Subscription subscription;
    std::deque<EventSample> queue;
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
  [[nodiscard]] core::Result<bool> AuthorizeServiceAccess(
    const std::optional<security::Principal>& principal,
    security::Operation operation,
    const ServiceIdentifier& service,
    std::string_view policy_id) const;

  [[nodiscard]] core::Result<std::string> ValidateOffer(const ServiceOffer& offer) const;
  [[nodiscard]] bool HasOfferedService(const ServiceIdentifier& service) const;
  [[nodiscard]] const ServiceOffer* OfferedService(const ServiceIdentifier& service) const;
  [[nodiscard]] static std::string KeyFor(const ServiceIdentifier& service);

  std::map<std::string, ServiceOffer> offers_;
  std::map<std::uint64_t, FindHandle> find_handles_;
  std::map<std::uint64_t, SubscriptionState> subscriptions_;
  const security::AccessPolicyEngine* access_policy_{nullptr};
  security::SecurityEventCollector* security_events_{nullptr};
  std::uint64_t next_handle_id_{1U};
  std::uint64_t next_subscription_id_{1U};
  std::uint64_t next_sequence_{1U};
};

}  // namespace openautosar::com
