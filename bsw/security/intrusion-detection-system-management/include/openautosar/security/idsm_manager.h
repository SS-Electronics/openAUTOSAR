// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/security/identity_access_manager.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::security::idsm {

enum class EventSource {
  kKernelAudit,
  kCommunication,
  kAuthentication,
  kUpdate,
  kDiagnostics,
  kFirewall,
  kHealthManager,
  kApplication,
};

enum class PrivacyMode {
  kPlain,
  kRedactPrincipal,
  kRedactIdentifiers,
};

enum class IdsmAction {
  kAccepted,
  kAggregated,
  kThrottled,
  kDroppedOldest,
  kRejected,
};

struct IdsmEvent final {
  EventSource source{EventSource::kApplication};
  std::string category;
  SecuritySeverity severity{SecuritySeverity::kInfo};
  std::string principal_id;
  std::string resource_id;
  Operation operation{Operation::kFindService};
  std::string detail;
  std::uint32_t count{1U};
  std::uint64_t timestamp_ms{0U};
};

struct IdsmPolicy final {
  std::size_t max_records{256U};
  std::uint64_t aggregation_window_ms{1'000U};
  std::uint32_t rate_limit_count{10U};
  std::uint64_t rate_limit_window_ms{1'000U};
  SecuritySeverity forwarding_threshold{SecuritySeverity::kCritical};
  bool backend_forwarding_enabled{false};
  PrivacyMode privacy_mode{PrivacyMode::kRedactPrincipal};
};

struct IdsmReceipt final {
  IdsmAction action{IdsmAction::kRejected};
  std::string reason;
  std::uint32_t count{0U};
  bool forwarded{false};
};

struct IdsmSnapshot final {
  std::vector<IdsmEvent> records;
  std::uint32_t dropped_events{0U};
  std::uint32_t throttled_events{0U};
  std::uint32_t forwarded_events{0U};
  std::uint32_t critical_events{0U};
};

class IdsmManager final {
public:
  explicit IdsmManager(IdsmPolicy policy = {});

  [[nodiscard]] core::Result<IdsmReceipt> Record(IdsmEvent event);
  [[nodiscard]] core::Result<IdsmReceipt> RecordSecurityEvent(
    const SecurityEvent& event);
  [[nodiscard]] IdsmSnapshot Snapshot() const;
  [[nodiscard]] std::vector<IdsmEvent> DrainForwardingBatch();

private:
  [[nodiscard]] bool ShouldForward(const IdsmEvent& event) const noexcept;
  [[nodiscard]] IdsmEvent Normalize(IdsmEvent event) const;

  IdsmPolicy policy_{};
  std::vector<IdsmEvent> records_;
  std::vector<IdsmEvent> forwarding_queue_;
  std::uint32_t dropped_events_{0U};
  std::uint32_t throttled_events_{0U};
  std::uint32_t forwarded_events_{0U};
};

[[nodiscard]] std::string_view ToString(EventSource source) noexcept;
[[nodiscard]] std::string_view ToString(PrivacyMode mode) noexcept;
[[nodiscard]] std::string_view ToString(IdsmAction action) noexcept;

}  // namespace openautosar::security::idsm
