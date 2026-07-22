// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>

namespace openautosar::com {

enum class Binding {
  kLocalIpc,
  kSomeIp,
  kDds,
};

enum class OfferState {
  kOffered,
  kStopped,
};

enum class HealthState {
  kHealthy,
  kDegraded,
  kFailed,
};

struct ServiceIdentifier final {
  std::uint32_t interface_id{0U};
  std::uint32_t instance_id{0U};
  std::uint16_t major_version{1U};
  std::uint16_t minor_version{0U};

  friend bool operator==(const ServiceIdentifier&, const ServiceIdentifier&) = default;
};

struct Endpoint final {
  Binding binding{Binding::kLocalIpc};
  std::string address;
  std::uint16_t port{0U};

  friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

struct ServiceOffer final {
  ServiceIdentifier service{};
  std::string process_identity;
  std::string machine_identity;
  Endpoint endpoint{};
  OfferState offer_state{OfferState::kOffered};
  HealthState health_state{HealthState::kHealthy};
  std::uint32_t ttl_ms{1'000U};
  std::string access_policy;
  std::string deployment_provenance;

  friend bool operator==(const ServiceOffer&, const ServiceOffer&) = default;
};

[[nodiscard]] const char* ToString(Binding binding) noexcept;
[[nodiscard]] const char* ToString(OfferState state) noexcept;
[[nodiscard]] const char* ToString(HealthState state) noexcept;

}  // namespace openautosar::com
