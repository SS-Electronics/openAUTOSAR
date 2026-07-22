// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openautosar::runtime::phm {

enum class HealthState {
  kHealthy,
  kDegraded,
  kFailed,
};

enum class SupervisionStatus {
  kOk,
  kMissingCheckpoint,
  kLogicalSequenceError,
  kDeadlineMissed,
  kProcessTerminated,
  kResourceExhausted,
  kServiceUnavailable,
  kRestartLimitExceeded,
};

enum class RecoveryAction {
  kIgnore,
  kRestartProcess,
  kRestartFunctionGroup,
  kEnterDegradedMode,
  kRequestMachineRestart,
  kTriggerExternalWatchdog,
  kRequestSafeState,
};

struct SupervisionPolicy final {
  std::string entity;
  std::vector<std::string> checkpoint_sequence;
  std::chrono::milliseconds alive_timeout{std::chrono::seconds(1)};
  std::chrono::milliseconds deadline_timeout{std::chrono::milliseconds(250)};
  std::uint32_t degraded_after_missed_alive{1U};
  std::uint32_t failed_after_missed_alive{3U};
  std::uint32_t max_restart_count{2U};
  std::uint64_t activation_time_ms{0U};
  RecoveryAction sequence_error_action{RecoveryAction::kRestartProcess};
  RecoveryAction deadline_miss_action{RecoveryAction::kRestartProcess};
  RecoveryAction degraded_action{RecoveryAction::kEnterDegradedMode};
  RecoveryAction failed_action{RecoveryAction::kRequestSafeState};
  RecoveryAction termination_action{RecoveryAction::kRestartProcess};
  RecoveryAction resource_exhaustion_action{RecoveryAction::kEnterDegradedMode};
  RecoveryAction service_unavailable_action{RecoveryAction::kEnterDegradedMode};
};

struct SupervisionReport final {
  std::string entity;
  HealthState health{HealthState::kHealthy};
  SupervisionStatus status{SupervisionStatus::kOk};
  RecoveryAction action{RecoveryAction::kIgnore};
  std::uint32_t missed_alive_count{0U};
  std::uint32_t restart_count{0U};
  std::uint64_t timestamp_ms{0U};
  std::optional<std::string> last_checkpoint;
  std::string detail;
};

class PlatformHealthManager final {
public:
  [[nodiscard]] core::Result<SupervisionReport> RegisterEntity(SupervisionPolicy policy);
  [[nodiscard]] core::Result<SupervisionReport> ReportCheckpoint(
    std::string_view entity,
    std::string_view checkpoint,
    std::uint64_t timestamp_ms);
  [[nodiscard]] core::Result<SupervisionReport> EvaluateAlive(
    std::string_view entity,
    std::uint64_t timestamp_ms);
  [[nodiscard]] std::vector<SupervisionReport> EvaluateAll(std::uint64_t timestamp_ms);

  [[nodiscard]] core::Result<SupervisionReport> ReportProcessTermination(
    std::string_view entity,
    std::uint64_t timestamp_ms);
  [[nodiscard]] core::Result<SupervisionReport> ReportResourceExhaustion(
    std::string_view entity,
    std::string detail,
    std::uint64_t timestamp_ms);
  [[nodiscard]] core::Result<SupervisionReport> ReportServiceAvailability(
    std::string_view entity,
    bool available,
    std::uint64_t timestamp_ms);
  [[nodiscard]] core::Result<SupervisionReport> ReportRestart(
    std::string_view entity,
    std::uint64_t timestamp_ms);

  [[nodiscard]] core::Result<SupervisionReport> ReportOf(std::string_view entity) const;

private:
  struct EntityState final {
    SupervisionPolicy policy;
    SupervisionReport report;
    std::size_t next_checkpoint_index{0U};
    std::optional<std::uint64_t> last_checkpoint_time_ms;
  };

  [[nodiscard]] core::Result<EntityState*> FindMutable(std::string_view entity);
  [[nodiscard]] core::Result<const EntityState*> Find(std::string_view entity) const;

  [[nodiscard]] static core::Result<bool> ValidatePolicy(const SupervisionPolicy& policy);
  [[nodiscard]] static SupervisionReport MakeReport(
    const SupervisionPolicy& policy,
    HealthState health,
    SupervisionStatus status,
    RecoveryAction action,
    std::uint64_t timestamp_ms,
    std::string detail);

  std::unordered_map<std::string, EntityState> entities_;
};

[[nodiscard]] std::string_view ToString(HealthState state) noexcept;
[[nodiscard]] std::string_view ToString(SupervisionStatus status) noexcept;
[[nodiscard]] std::string_view ToString(RecoveryAction action) noexcept;

}  // namespace openautosar::runtime::phm
