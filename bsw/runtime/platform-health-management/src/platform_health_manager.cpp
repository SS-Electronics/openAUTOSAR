// SPDX-License-Identifier: MIT

#include "openautosar/runtime/platform_health_manager.h"

#include <algorithm>
#include <utility>

namespace openautosar::runtime::phm {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"platform-health-management", message};
}

[[nodiscard]] std::uint32_t MissedAliveWindows(
  const SupervisionPolicy& policy,
  const std::optional<std::uint64_t>& last_checkpoint_time_ms,
  std::uint64_t timestamp_ms) noexcept {
  const auto timeout = static_cast<std::uint64_t>(policy.alive_timeout.count());
  if (timeout == 0U) {
    return 0U;
  }

  const auto reference_time = last_checkpoint_time_ms.value_or(policy.activation_time_ms);
  if (timestamp_ms <= reference_time + timeout) {
    return 0U;
  }

  const auto elapsed_after_timeout = timestamp_ms - reference_time - timeout;
  return static_cast<std::uint32_t>((elapsed_after_timeout / timeout) + 1U);
}

[[nodiscard]] HealthState HealthForMissedAlive(
  const SupervisionPolicy& policy,
  std::uint32_t missed_alive_count) noexcept {
  if (missed_alive_count >= policy.failed_after_missed_alive) {
    return HealthState::kFailed;
  }

  if (missed_alive_count >= policy.degraded_after_missed_alive) {
    return HealthState::kDegraded;
  }

  return HealthState::kHealthy;
}

[[nodiscard]] RecoveryAction ActionForHealth(
  const SupervisionPolicy& policy,
  HealthState health) noexcept {
  switch (health) {
    case HealthState::kHealthy:
      return RecoveryAction::kIgnore;
    case HealthState::kDegraded:
      return policy.degraded_action;
    case HealthState::kFailed:
      return policy.failed_action;
  }

  return RecoveryAction::kIgnore;
}

}  // namespace

core::Result<SupervisionReport> PlatformHealthManager::RegisterEntity(SupervisionPolicy policy) {
  auto validation = ValidatePolicy(policy);
  if (!validation) {
    return core::Result<SupervisionReport>::FromError(validation.Error());
  }

  if (entities_.find(policy.entity) != entities_.end()) {
    return core::Result<SupervisionReport>::FromError(MakeError("entity is already registered"));
  }

  auto report = MakeReport(
    policy,
    HealthState::kHealthy,
    SupervisionStatus::kOk,
    RecoveryAction::kIgnore,
    policy.activation_time_ms,
    "registered");
  const auto entity = policy.entity;
  entities_.emplace(entity, EntityState{
                            .policy = std::move(policy),
                            .report = report,
                            .next_checkpoint_index = 0U,
                            .last_checkpoint_time_ms = std::nullopt,
                          });
  return core::Result<SupervisionReport>::FromValue(std::move(report));
}

core::Result<SupervisionReport> PlatformHealthManager::ReportCheckpoint(
  std::string_view entity,
  std::string_view checkpoint,
  std::uint64_t timestamp_ms) {
  auto state = FindMutable(entity);
  if (!state) {
    return core::Result<SupervisionReport>::FromError(state.Error());
  }

  auto& record = *state.Value();
  if (checkpoint.empty()) {
    return core::Result<SupervisionReport>::FromError(MakeError("checkpoint is empty"));
  }

  if (!record.policy.checkpoint_sequence.empty()) {
    const auto& expected = record.policy.checkpoint_sequence[record.next_checkpoint_index];
    if (checkpoint != expected) {
      record.report = MakeReport(
        record.policy,
        HealthState::kFailed,
        SupervisionStatus::kLogicalSequenceError,
        record.policy.sequence_error_action,
        timestamp_ms,
        "expected checkpoint " + expected);
      return core::Result<SupervisionReport>::FromValue(record.report);
    }

    record.next_checkpoint_index =
      (record.next_checkpoint_index + 1U) % record.policy.checkpoint_sequence.size();
  }

  if (record.last_checkpoint_time_ms.has_value() &&
      record.report.status == SupervisionStatus::kOk) {
    const auto elapsed = timestamp_ms - record.last_checkpoint_time_ms.value();
    if (elapsed > static_cast<std::uint64_t>(record.policy.deadline_timeout.count())) {
      record.last_checkpoint_time_ms = timestamp_ms;
      record.report = MakeReport(
        record.policy,
        HealthState::kDegraded,
        SupervisionStatus::kDeadlineMissed,
        record.policy.deadline_miss_action,
        timestamp_ms,
        "checkpoint deadline exceeded");
      record.report.last_checkpoint = std::string(checkpoint);
      return core::Result<SupervisionReport>::FromValue(record.report);
    }
  }

  record.last_checkpoint_time_ms = timestamp_ms;
  record.report = MakeReport(
    record.policy,
    HealthState::kHealthy,
    SupervisionStatus::kOk,
    RecoveryAction::kIgnore,
    timestamp_ms,
    "checkpoint accepted");
  record.report.last_checkpoint = std::string(checkpoint);
  return core::Result<SupervisionReport>::FromValue(record.report);
}

core::Result<SupervisionReport> PlatformHealthManager::EvaluateAlive(
  std::string_view entity,
  std::uint64_t timestamp_ms) {
  auto state = FindMutable(entity);
  if (!state) {
    return core::Result<SupervisionReport>::FromError(state.Error());
  }

  auto& record = *state.Value();
  const auto missed_alive_count =
    MissedAliveWindows(record.policy, record.last_checkpoint_time_ms, timestamp_ms);
  const auto health = HealthForMissedAlive(record.policy, missed_alive_count);
  const auto last_checkpoint = record.report.last_checkpoint;
  const auto status =
    missed_alive_count == 0U ? SupervisionStatus::kOk : SupervisionStatus::kMissingCheckpoint;
  const auto action = ActionForHealth(record.policy, health);
  record.report = MakeReport(
    record.policy,
    health,
    status,
    action,
    timestamp_ms,
    missed_alive_count == 0U ? "alive supervision satisfied" : "alive checkpoint missing");
  record.report.missed_alive_count = missed_alive_count;
  record.report.last_checkpoint =
    record.last_checkpoint_time_ms.has_value() ? last_checkpoint : std::nullopt;
  return core::Result<SupervisionReport>::FromValue(record.report);
}

std::vector<SupervisionReport> PlatformHealthManager::EvaluateAll(std::uint64_t timestamp_ms) {
  std::vector<SupervisionReport> reports;
  reports.reserve(entities_.size());
  for (auto& [entity, _] : entities_) {
    auto report = EvaluateAlive(entity, timestamp_ms);
    if (report) {
      reports.push_back(std::move(report.Value()));
    }
  }

  std::sort(reports.begin(), reports.end(), [](const auto& left, const auto& right) {
    return left.entity < right.entity;
  });
  return reports;
}

core::Result<SupervisionReport> PlatformHealthManager::ReportProcessTermination(
  std::string_view entity,
  std::uint64_t timestamp_ms) {
  auto state = FindMutable(entity);
  if (!state) {
    return core::Result<SupervisionReport>::FromError(state.Error());
  }

  auto& record = *state.Value();
  record.report = MakeReport(
    record.policy,
    HealthState::kFailed,
    SupervisionStatus::kProcessTerminated,
    record.policy.termination_action,
    timestamp_ms,
    "process terminated");
  return core::Result<SupervisionReport>::FromValue(record.report);
}

core::Result<SupervisionReport> PlatformHealthManager::ReportResourceExhaustion(
  std::string_view entity,
  std::string detail,
  std::uint64_t timestamp_ms) {
  auto state = FindMutable(entity);
  if (!state) {
    return core::Result<SupervisionReport>::FromError(state.Error());
  }

  if (detail.empty()) {
    return core::Result<SupervisionReport>::FromError(MakeError("resource detail is empty"));
  }

  auto& record = *state.Value();
  record.report = MakeReport(
    record.policy,
    HealthState::kDegraded,
    SupervisionStatus::kResourceExhausted,
    record.policy.resource_exhaustion_action,
    timestamp_ms,
    std::move(detail));
  return core::Result<SupervisionReport>::FromValue(record.report);
}

core::Result<SupervisionReport> PlatformHealthManager::ReportServiceAvailability(
  std::string_view entity,
  bool available,
  std::uint64_t timestamp_ms) {
  auto state = FindMutable(entity);
  if (!state) {
    return core::Result<SupervisionReport>::FromError(state.Error());
  }

  auto& record = *state.Value();
  if (available) {
    record.report = MakeReport(
      record.policy,
      HealthState::kHealthy,
      SupervisionStatus::kOk,
      RecoveryAction::kIgnore,
      timestamp_ms,
      "service available");
    return core::Result<SupervisionReport>::FromValue(record.report);
  }

  record.report = MakeReport(
    record.policy,
    HealthState::kDegraded,
    SupervisionStatus::kServiceUnavailable,
    record.policy.service_unavailable_action,
    timestamp_ms,
    "service unavailable");
  return core::Result<SupervisionReport>::FromValue(record.report);
}

core::Result<SupervisionReport> PlatformHealthManager::ReportRestart(
  std::string_view entity,
  std::uint64_t timestamp_ms) {
  auto state = FindMutable(entity);
  if (!state) {
    return core::Result<SupervisionReport>::FromError(state.Error());
  }

  auto& record = *state.Value();
  record.report.restart_count += 1U;
  if (record.report.restart_count > record.policy.max_restart_count) {
    record.report = MakeReport(
      record.policy,
      HealthState::kFailed,
      SupervisionStatus::kRestartLimitExceeded,
      record.policy.failed_action,
      timestamp_ms,
      "restart limit exceeded");
    record.report.restart_count = record.policy.max_restart_count + 1U;
    return core::Result<SupervisionReport>::FromValue(record.report);
  }

  record.report.timestamp_ms = timestamp_ms;
  record.report.detail = "restart recorded";
  return core::Result<SupervisionReport>::FromValue(record.report);
}

core::Result<SupervisionReport> PlatformHealthManager::ReportOf(std::string_view entity) const {
  auto state = Find(entity);
  if (!state) {
    return core::Result<SupervisionReport>::FromError(state.Error());
  }

  return core::Result<SupervisionReport>::FromValue(state.Value()->report);
}

core::Result<PlatformHealthManager::EntityState*> PlatformHealthManager::FindMutable(
  std::string_view entity) {
  const auto iter = entities_.find(std::string(entity));
  if (iter == entities_.end()) {
    return core::Result<EntityState*>::FromError(MakeError("entity is not registered"));
  }

  return core::Result<EntityState*>::FromValue(&iter->second);
}

core::Result<const PlatformHealthManager::EntityState*> PlatformHealthManager::Find(
  std::string_view entity) const {
  const auto iter = entities_.find(std::string(entity));
  if (iter == entities_.end()) {
    return core::Result<const EntityState*>::FromError(MakeError("entity is not registered"));
  }

  return core::Result<const EntityState*>::FromValue(&iter->second);
}

core::Result<bool> PlatformHealthManager::ValidatePolicy(const SupervisionPolicy& policy) {
  if (policy.entity.empty()) {
    return core::Result<bool>::FromError(MakeError("supervised entity is empty"));
  }

  if (policy.alive_timeout.count() <= 0 || policy.deadline_timeout.count() <= 0) {
    return core::Result<bool>::FromError(MakeError("supervision timeout is invalid"));
  }

  if (policy.degraded_after_missed_alive == 0U ||
      policy.failed_after_missed_alive < policy.degraded_after_missed_alive) {
    return core::Result<bool>::FromError(MakeError("alive supervision thresholds are invalid"));
  }

  for (const auto& checkpoint : policy.checkpoint_sequence) {
    if (checkpoint.empty()) {
      return core::Result<bool>::FromError(MakeError("checkpoint sequence contains an empty name"));
    }
  }

  return core::Result<bool>::FromValue(true);
}

SupervisionReport PlatformHealthManager::MakeReport(
  const SupervisionPolicy& policy,
  HealthState health,
  SupervisionStatus status,
  RecoveryAction action,
  std::uint64_t timestamp_ms,
  std::string detail) {
  return {
    .entity = policy.entity,
    .health = health,
    .status = status,
    .action = action,
    .missed_alive_count = 0U,
    .restart_count = 0U,
    .timestamp_ms = timestamp_ms,
    .last_checkpoint = std::nullopt,
    .detail = std::move(detail),
  };
}

std::string_view ToString(HealthState state) noexcept {
  switch (state) {
    case HealthState::kHealthy:
      return "Healthy";
    case HealthState::kDegraded:
      return "Degraded";
    case HealthState::kFailed:
      return "Failed";
  }

  return "Unknown";
}

std::string_view ToString(SupervisionStatus status) noexcept {
  switch (status) {
    case SupervisionStatus::kOk:
      return "Ok";
    case SupervisionStatus::kMissingCheckpoint:
      return "MissingCheckpoint";
    case SupervisionStatus::kLogicalSequenceError:
      return "LogicalSequenceError";
    case SupervisionStatus::kDeadlineMissed:
      return "DeadlineMissed";
    case SupervisionStatus::kProcessTerminated:
      return "ProcessTerminated";
    case SupervisionStatus::kResourceExhausted:
      return "ResourceExhausted";
    case SupervisionStatus::kServiceUnavailable:
      return "ServiceUnavailable";
    case SupervisionStatus::kRestartLimitExceeded:
      return "RestartLimitExceeded";
  }

  return "Unknown";
}

std::string_view ToString(RecoveryAction action) noexcept {
  switch (action) {
    case RecoveryAction::kIgnore:
      return "Ignore";
    case RecoveryAction::kRestartProcess:
      return "RestartProcess";
    case RecoveryAction::kRestartFunctionGroup:
      return "RestartFunctionGroup";
    case RecoveryAction::kEnterDegradedMode:
      return "EnterDegradedMode";
    case RecoveryAction::kRequestMachineRestart:
      return "RequestMachineRestart";
    case RecoveryAction::kTriggerExternalWatchdog:
      return "TriggerExternalWatchdog";
    case RecoveryAction::kRequestSafeState:
      return "RequestSafeState";
  }

  return "Unknown";
}

}  // namespace openautosar::runtime::phm
