// SPDX-License-Identifier: MIT

#include "openautosar/security/idsm_manager.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace openautosar::security::idsm {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"idsm", message};
}

[[nodiscard]] std::uint32_t SeverityRank(SecuritySeverity severity) noexcept {
  switch (severity) {
    case SecuritySeverity::kInfo:
      return 0U;
    case SecuritySeverity::kWarning:
      return 1U;
    case SecuritySeverity::kCritical:
      return 2U;
  }

  return 0U;
}

[[nodiscard]] bool SameAggregationKey(
  const IdsmEvent& left,
  const IdsmEvent& right) noexcept {
  return left.source == right.source &&
         left.category == right.category &&
         left.severity == right.severity &&
         left.principal_id == right.principal_id &&
         left.resource_id == right.resource_id &&
         left.operation == right.operation &&
         left.detail == right.detail;
}

[[nodiscard]] std::uint64_t ElapsedMs(
  std::uint64_t newer,
  std::uint64_t older) noexcept {
  return newer >= older ? newer - older : 0U;
}

[[nodiscard]] std::uint32_t SaturatingAdd(
  std::uint32_t left,
  std::uint32_t right) noexcept {
  const auto maximum = std::numeric_limits<std::uint32_t>::max();
  if (maximum - left < right) {
    return maximum;
  }
  return left + right;
}

[[nodiscard]] EventSource SourceFromSecurityEvent(std::string_view source) noexcept {
  if (source == "ara-com" || source == "communication") {
    return EventSource::kCommunication;
  }
  if (source == "diagnostics") {
    return EventSource::kDiagnostics;
  }
  if (source == "firewall") {
    return EventSource::kFirewall;
  }
  if (source == "ucm" || source == "update") {
    return EventSource::kUpdate;
  }
  if (source == "phm" || source == "health-manager") {
    return EventSource::kHealthManager;
  }
  if (source == "kernel" || source == "audit") {
    return EventSource::kKernelAudit;
  }
  if (source == "iam" || source == "authentication") {
    return EventSource::kAuthentication;
  }
  return EventSource::kApplication;
}

}  // namespace

IdsmManager::IdsmManager(IdsmPolicy policy) : policy_(policy) {}

core::Result<IdsmReceipt> IdsmManager::Record(IdsmEvent event) {
  if (policy_.max_records == 0U) {
    return core::Result<IdsmReceipt>::FromError(MakeError("IDSM record budget is zero"));
  }

  if (event.category.empty() || event.detail.empty()) {
    return core::Result<IdsmReceipt>::FromError(MakeError("IDSM event is incomplete"));
  }

  if (event.count == 0U) {
    event.count = 1U;
  }

  event = Normalize(std::move(event));
  for (auto& stored : records_) {
    if (!SameAggregationKey(stored, event)) {
      continue;
    }

    const auto elapsed = ElapsedMs(event.timestamp_ms, stored.timestamp_ms);
    if (elapsed > policy_.aggregation_window_ms) {
      continue;
    }

    stored.count = SaturatingAdd(stored.count, event.count);
    stored.timestamp_ms = event.timestamp_ms;
    if (stored.count > policy_.rate_limit_count &&
        elapsed <= policy_.rate_limit_window_ms) {
      ++throttled_events_;
      return core::Result<IdsmReceipt>::FromValue({
        .action = IdsmAction::kThrottled,
        .reason = "IDSM event was rate limited",
        .count = stored.count,
        .forwarded = false,
      });
    }

    const bool forwarded = ShouldForward(stored);
    if (forwarded) {
      forwarding_queue_.push_back(stored);
      forwarded_events_ = SaturatingAdd(forwarded_events_, stored.count);
    }
    return core::Result<IdsmReceipt>::FromValue({
      .action = IdsmAction::kAggregated,
      .reason = "IDSM event aggregated",
      .count = stored.count,
      .forwarded = forwarded,
    });
  }

  IdsmAction action{IdsmAction::kAccepted};
  if (records_.size() >= policy_.max_records) {
    records_.erase(records_.begin());
    ++dropped_events_;
    action = IdsmAction::kDroppedOldest;
  }

  const bool forwarded = ShouldForward(event);
  if (forwarded) {
    forwarding_queue_.push_back(event);
    forwarded_events_ = SaturatingAdd(forwarded_events_, event.count);
  }
  const auto count = event.count;
  records_.push_back(std::move(event));
  return core::Result<IdsmReceipt>::FromValue({
    .action = action,
    .reason = action == IdsmAction::kDroppedOldest ? "oldest IDSM event dropped"
                                                   : "IDSM event accepted",
    .count = count,
    .forwarded = forwarded,
  });
}

core::Result<IdsmReceipt> IdsmManager::RecordSecurityEvent(
  const SecurityEvent& event) {
  return Record({
    .source = SourceFromSecurityEvent(event.source),
    .category = event.category,
    .severity = event.severity,
    .principal_id = event.principal_id,
    .resource_id = event.resource_id,
    .operation = event.operation,
    .detail = event.detail,
    .count = event.count,
    .timestamp_ms = event.timestamp_ms,
  });
}

IdsmSnapshot IdsmManager::Snapshot() const {
  std::uint32_t critical_count{0U};
  for (const auto& record : records_) {
    if (record.severity == SecuritySeverity::kCritical) {
      critical_count = SaturatingAdd(critical_count, record.count);
    }
  }

  return {
    .records = records_,
    .dropped_events = dropped_events_,
    .throttled_events = throttled_events_,
    .forwarded_events = forwarded_events_,
    .critical_events = critical_count,
  };
}

std::vector<IdsmEvent> IdsmManager::DrainForwardingBatch() {
  auto batch = std::move(forwarding_queue_);
  forwarding_queue_.clear();
  return batch;
}

bool IdsmManager::ShouldForward(const IdsmEvent& event) const noexcept {
  return policy_.backend_forwarding_enabled &&
         SeverityRank(event.severity) >= SeverityRank(policy_.forwarding_threshold);
}

IdsmEvent IdsmManager::Normalize(IdsmEvent event) const {
  if (policy_.privacy_mode == PrivacyMode::kRedactPrincipal ||
      policy_.privacy_mode == PrivacyMode::kRedactIdentifiers) {
    if (!event.principal_id.empty()) {
      event.principal_id = "redacted";
    }
  }

  if (policy_.privacy_mode == PrivacyMode::kRedactIdentifiers &&
      !event.resource_id.empty()) {
    event.resource_id = "redacted";
  }

  return event;
}

std::string_view ToString(EventSource source) noexcept {
  switch (source) {
    case EventSource::kKernelAudit:
      return "kernel-audit";
    case EventSource::kCommunication:
      return "communication";
    case EventSource::kAuthentication:
      return "authentication";
    case EventSource::kUpdate:
      return "update";
    case EventSource::kDiagnostics:
      return "diagnostics";
    case EventSource::kFirewall:
      return "firewall";
    case EventSource::kHealthManager:
      return "health-manager";
    case EventSource::kApplication:
      return "application";
  }

  return "unknown";
}

std::string_view ToString(PrivacyMode mode) noexcept {
  switch (mode) {
    case PrivacyMode::kPlain:
      return "plain";
    case PrivacyMode::kRedactPrincipal:
      return "redact-principal";
    case PrivacyMode::kRedactIdentifiers:
      return "redact-identifiers";
  }

  return "unknown";
}

std::string_view ToString(IdsmAction action) noexcept {
  switch (action) {
    case IdsmAction::kAccepted:
      return "accepted";
    case IdsmAction::kAggregated:
      return "aggregated";
    case IdsmAction::kThrottled:
      return "throttled";
    case IdsmAction::kDroppedOldest:
      return "dropped-oldest";
    case IdsmAction::kRejected:
      return "rejected";
  }

  return "unknown";
}

}  // namespace openautosar::security::idsm
