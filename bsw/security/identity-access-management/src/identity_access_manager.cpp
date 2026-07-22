// SPDX-License-Identifier: MIT

#include "openautosar/security/identity_access_manager.h"

#include <algorithm>
#include <utility>

namespace openautosar::security {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"identity-access-management", message};
}

template <typename T>
[[nodiscard]] bool Contains(const std::vector<T>& values, const T& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool HasAnyRole(
  const std::vector<std::string>& principal_roles,
  const std::vector<std::string>& rule_roles) {
  if (rule_roles.empty()) {
    return true;
  }

  return std::any_of(
    principal_roles.begin(),
    principal_roles.end(),
    [&rule_roles](const std::string& role) { return Contains(rule_roles, role); });
}

[[nodiscard]] bool MatchesAnyPrefix(
  std::string_view value,
  const std::vector<std::string>& prefixes) {
  if (prefixes.empty()) {
    return true;
  }

  return std::any_of(
    prefixes.begin(),
    prefixes.end(),
    [value](const std::string& prefix) { return value.rfind(prefix, 0U) == 0U; });
}

[[nodiscard]] bool MatchesRule(
  const AccessRule& rule,
  const AuthorizationRequest& request) {
  if (!rule.operations.empty() && !Contains(rule.operations, request.operation)) {
    return false;
  }

  if (!rule.resource_kinds.empty() &&
      !Contains(rule.resource_kinds, request.resource.kind)) {
    return false;
  }

  if (!MatchesAnyPrefix(request.resource.identifier, rule.resource_prefixes) &&
      !MatchesAnyPrefix(request.resource.policy_id, rule.resource_prefixes)) {
    return false;
  }

  if (!rule.principal_ids.empty() &&
      !Contains(rule.principal_ids, request.principal.application_id)) {
    return false;
  }

  if (!rule.machine_ids.empty() && !Contains(rule.machine_ids, request.principal.machine_id)) {
    return false;
  }

  if (!HasAnyRole(request.principal.roles, rule.roles)) {
    return false;
  }

  if (rule.require_authenticated && !request.principal.authenticated) {
    return false;
  }

  return rule.allow_remote || !request.principal.remote;
}

[[nodiscard]] bool IsDiagnosticOperation(Operation operation) noexcept {
  return operation == Operation::kDiagnosticRead ||
         operation == Operation::kDiagnosticSecurityAccess ||
         operation == Operation::kDiagnosticClearDtc ||
         operation == Operation::kDiagnosticRoutineControl;
}

[[nodiscard]] bool SameAggregationKey(
  const SecurityEvent& left,
  const SecurityEvent& right) noexcept {
  return left.source == right.source &&
         left.category == right.category &&
         left.principal_id == right.principal_id &&
         left.resource_id == right.resource_id &&
         left.operation == right.operation &&
         left.detail == right.detail &&
         left.severity == right.severity;
}

}  // namespace

AccessPolicyEngine::AccessPolicyEngine(SecurityPolicy policy) : policy_(std::move(policy)) {}

AuthorizationDecision AccessPolicyEngine::Authorize(
  const AuthorizationRequest& request) const {
  if (policy_.production_mode && !request.principal.authenticated) {
    return {
      .decision = Decision::kDeny,
      .severity = SecuritySeverity::kCritical,
      .reason = "unauthenticated principal rejected in production mode",
    };
  }

  if (request.principal.remote && IsDiagnosticOperation(request.operation) &&
      !policy_.remote_diagnostics_allowed) {
    return {
      .decision = Decision::kDeny,
      .severity = SecuritySeverity::kCritical,
      .reason = "remote diagnostic access is disabled by policy",
    };
  }

  std::optional<AuthorizationDecision> allow;
  for (const auto& rule : policy_.rules) {
    if (!MatchesRule(rule, request)) {
      continue;
    }

    AuthorizationDecision decision{
      .decision = rule.decision,
      .severity = rule.decision == Decision::kAllow ? SecuritySeverity::kInfo
                                                    : SecuritySeverity::kWarning,
      .reason = rule.reason.empty() ? std::string(ToString(rule.decision)) : rule.reason,
    };
    if (rule.decision == Decision::kDeny) {
      return decision;
    }

    allow = std::move(decision);
  }

  if (allow.has_value()) {
    return allow.value();
  }

  return {
    .decision = policy_.default_decision,
    .severity = policy_.default_decision == Decision::kAllow ? SecuritySeverity::kInfo
                                                             : SecuritySeverity::kWarning,
    .reason = "no matching access rule",
  };
}

void AccessPolicyEngine::AddRule(AccessRule rule) {
  policy_.rules.push_back(std::move(rule));
}

core::Result<bool> TrustStore::TrustSigner(SignerTrust trust) {
  if (trust.signer_id.empty() || trust.key_slot.empty()) {
    return core::Result<bool>::FromError(MakeError("signer trust record is invalid"));
  }

  auto iter = std::find_if(
    signers_.begin(),
    signers_.end(),
    [&trust](const SignerTrust& item) { return item.signer_id == trust.signer_id; });
  if (iter == signers_.end()) {
    signers_.push_back(std::move(trust));
  } else {
    *iter = std::move(trust);
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> TrustStore::RevokeSigner(std::string_view signer_id) {
  auto iter = std::find_if(
    signers_.begin(),
    signers_.end(),
    [signer_id](const SignerTrust& item) { return item.signer_id == signer_id; });
  if (iter == signers_.end()) {
    return core::Result<bool>::FromError(MakeError("signer is not trusted"));
  }

  iter->revoked = true;
  return core::Result<bool>::FromValue(true);
}

bool TrustStore::IsSignerTrusted(
  std::string_view signer_id,
  std::uint64_t package_counter) const noexcept {
  const auto iter = std::find_if(
    signers_.begin(),
    signers_.end(),
    [signer_id](const SignerTrust& item) { return item.signer_id == signer_id; });
  return iter != signers_.end() && !iter->revoked &&
         package_counter >= iter->minimum_counter;
}

std::optional<std::uint64_t> TrustStore::AntiRollbackCounter(
  std::string_view signer_id) const {
  const auto iter = std::find_if(
    signers_.begin(),
    signers_.end(),
    [signer_id](const SignerTrust& item) { return item.signer_id == signer_id; });
  if (iter == signers_.end()) {
    return std::nullopt;
  }

  return iter->minimum_counter;
}

SecurityEventCollector::SecurityEventCollector(SecurityEventPolicy policy)
  : policy_(policy) {}

core::Result<bool> SecurityEventCollector::Record(SecurityEvent event) {
  if (event.source.empty() || event.category.empty() || event.detail.empty()) {
    return core::Result<bool>::FromError(MakeError("security event is invalid"));
  }

  if (event.count == 0U) {
    event.count = 1U;
  }

  for (auto& stored : events_) {
    if (!SameAggregationKey(stored, event)) {
      continue;
    }

    const auto elapsed = event.timestamp_ms >= stored.timestamp_ms
                           ? event.timestamp_ms - stored.timestamp_ms
                           : 0U;
    if (elapsed <= policy_.aggregation_window_ms) {
      stored.count += event.count;
      stored.timestamp_ms = event.timestamp_ms;
      return core::Result<bool>::FromValue(true);
    }
  }

  if (events_.size() >= policy_.max_events) {
    events_.erase(events_.begin());
  }
  events_.push_back(std::move(event));
  return core::Result<bool>::FromValue(true);
}

std::uint32_t SecurityEventCollector::CountBySeverity(
  SecuritySeverity severity) const noexcept {
  std::uint32_t count{0U};
  for (const auto& event : events_) {
    if (event.severity == severity) {
      count += event.count;
    }
  }
  return count;
}

std::string_view ToString(Decision decision) noexcept {
  switch (decision) {
    case Decision::kAllow:
      return "Allow";
    case Decision::kDeny:
      return "Deny";
  }

  return "Unknown";
}

std::string_view ToString(ResourceKind kind) noexcept {
  switch (kind) {
    case ResourceKind::kService:
      return "Service";
    case ResourceKind::kDiagnostic:
      return "Diagnostic";
    case ResourceKind::kUpdate:
      return "Update";
    case ResourceKind::kPersistency:
      return "Persistency";
    case ResourceKind::kFirewall:
      return "Firewall";
    case ResourceKind::kApiGateway:
      return "ApiGateway";
    case ResourceKind::kApplication:
      return "Application";
  }

  return "Unknown";
}

std::string_view ToString(Operation operation) noexcept {
  switch (operation) {
    case Operation::kOfferService:
      return "OfferService";
    case Operation::kFindService:
      return "FindService";
    case Operation::kSubscribeEvent:
      return "SubscribeEvent";
    case Operation::kPublishEvent:
      return "PublishEvent";
    case Operation::kDiagnosticRead:
      return "DiagnosticRead";
    case Operation::kDiagnosticSecurityAccess:
      return "DiagnosticSecurityAccess";
    case Operation::kDiagnosticClearDtc:
      return "DiagnosticClearDtc";
    case Operation::kDiagnosticRoutineControl:
      return "DiagnosticRoutineControl";
    case Operation::kUpdateVerify:
      return "UpdateVerify";
    case Operation::kPersistencyRead:
      return "PersistencyRead";
    case Operation::kPersistencyWrite:
      return "PersistencyWrite";
    case Operation::kPersistencyRemove:
      return "PersistencyRemove";
    case Operation::kInstallFirewallRule:
      return "InstallFirewallRule";
    case Operation::kInvokeGatewayRoute:
      return "InvokeGatewayRoute";
  }

  return "Unknown";
}

std::string_view ToString(SecuritySeverity severity) noexcept {
  switch (severity) {
    case SecuritySeverity::kInfo:
      return "Info";
    case SecuritySeverity::kWarning:
      return "Warning";
    case SecuritySeverity::kCritical:
      return "Critical";
  }

  return "Unknown";
}

}  // namespace openautosar::security
