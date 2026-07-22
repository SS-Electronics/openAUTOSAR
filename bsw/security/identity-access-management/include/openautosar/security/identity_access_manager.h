// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::security {

enum class Decision {
  kAllow,
  kDeny,
};

enum class ResourceKind {
  kService,
  kDiagnostic,
  kUpdate,
  kPersistency,
  kFirewall,
  kApiGateway,
  kApplication,
};

enum class Operation {
  kOfferService,
  kFindService,
  kSubscribeEvent,
  kPublishEvent,
  kCallMethod,
  kCompleteMethod,
  kGetField,
  kSetField,
  kSubscribeField,
  kSubscribeTrigger,
  kFireTrigger,
  kDiagnosticRead,
  kDiagnosticSecurityAccess,
  kDiagnosticClearDtc,
  kDiagnosticRoutineControl,
  kUpdateVerify,
  kPersistencyRead,
  kPersistencyWrite,
  kPersistencyRemove,
  kInstallFirewallRule,
  kInvokeGatewayRoute,
};

enum class SecuritySeverity {
  kInfo,
  kWarning,
  kCritical,
};

struct Principal final {
  std::string application_id;
  std::string machine_id;
  std::string security_label;
  std::vector<std::string> roles;
  bool authenticated{false};
  bool remote{false};
};

struct Resource final {
  ResourceKind kind{ResourceKind::kApplication};
  std::string identifier;
  std::string policy_id;
};

struct AuthorizationRequest final {
  Principal principal;
  Resource resource;
  Operation operation{Operation::kFindService};
  std::string action;
};

struct AuthorizationDecision final {
  Decision decision{Decision::kDeny};
  SecuritySeverity severity{SecuritySeverity::kWarning};
  std::string reason;

  [[nodiscard]] bool Allowed() const noexcept { return decision == Decision::kAllow; }
};

struct AccessRule final {
  Decision decision{Decision::kDeny};
  std::vector<Operation> operations;
  std::vector<ResourceKind> resource_kinds;
  std::vector<std::string> resource_prefixes;
  std::vector<std::string> principal_ids;
  std::vector<std::string> machine_ids;
  std::vector<std::string> roles;
  bool require_authenticated{true};
  bool allow_remote{false};
  std::string reason;
};

struct SecurityPolicy final {
  bool production_mode{true};
  bool remote_diagnostics_allowed{false};
  Decision default_decision{Decision::kDeny};
  std::vector<AccessRule> rules;
};

class AccessPolicyEngine final {
public:
  explicit AccessPolicyEngine(SecurityPolicy policy = {});

  [[nodiscard]] AuthorizationDecision Authorize(
    const AuthorizationRequest& request) const;
  [[nodiscard]] const SecurityPolicy& Policy() const noexcept { return policy_; }

  void AddRule(AccessRule rule);

private:
  SecurityPolicy policy_{};
};

struct SignerTrust final {
  std::string signer_id;
  std::string key_slot;
  std::string certificate_id;
  std::uint64_t minimum_counter{0U};
  bool revoked{false};
};

class TrustStore final {
public:
  [[nodiscard]] core::Result<bool> TrustSigner(SignerTrust trust);
  [[nodiscard]] core::Result<bool> RevokeSigner(std::string_view signer_id);
  [[nodiscard]] bool IsSignerTrusted(
    std::string_view signer_id,
    std::uint64_t package_counter) const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> AntiRollbackCounter(
    std::string_view signer_id) const;

private:
  std::vector<SignerTrust> signers_;
};

struct SecurityEvent final {
  std::string source;
  std::string category;
  SecuritySeverity severity{SecuritySeverity::kInfo};
  std::string principal_id;
  std::string resource_id;
  Operation operation{Operation::kFindService};
  std::string detail;
  std::uint32_t count{1U};
  std::uint64_t timestamp_ms{0U};
};

struct SecurityEventPolicy final {
  std::size_t max_events{256U};
  std::uint64_t aggregation_window_ms{1'000U};
};

class SecurityEventCollector final {
public:
  explicit SecurityEventCollector(SecurityEventPolicy policy = {});

  [[nodiscard]] core::Result<bool> Record(SecurityEvent event);
  [[nodiscard]] std::vector<SecurityEvent> Snapshot() const { return events_; }
  [[nodiscard]] std::uint32_t CountBySeverity(SecuritySeverity severity) const noexcept;

private:
  SecurityEventPolicy policy_{};
  std::vector<SecurityEvent> events_;
};

[[nodiscard]] std::string_view ToString(Decision decision) noexcept;
[[nodiscard]] std::string_view ToString(ResourceKind kind) noexcept;
[[nodiscard]] std::string_view ToString(Operation operation) noexcept;
[[nodiscard]] std::string_view ToString(SecuritySeverity severity) noexcept;

}  // namespace openautosar::security
