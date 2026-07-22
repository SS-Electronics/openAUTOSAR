// SPDX-License-Identifier: MIT

#include "openautosar/runtime/platform_health_manager.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

openautosar::runtime::phm::SupervisionPolicy TestPolicy(std::string entity) {
  using namespace std::chrono_literals;
  using openautosar::runtime::phm::RecoveryAction;

  return {
    .entity = std::move(entity),
    .checkpoint_sequence = {"Init", "ServiceOffered", "Alive"},
    .alive_timeout = 100ms,
    .deadline_timeout = 50ms,
    .degraded_after_missed_alive = 1U,
    .failed_after_missed_alive = 3U,
    .max_restart_count = 1U,
    .activation_time_ms = 1'000U,
    .sequence_error_action = RecoveryAction::kRestartProcess,
    .deadline_miss_action = RecoveryAction::kRestartProcess,
    .degraded_action = RecoveryAction::kEnterDegradedMode,
    .failed_action = RecoveryAction::kRequestSafeState,
    .termination_action = RecoveryAction::kRestartProcess,
    .resource_exhaustion_action = RecoveryAction::kEnterDegradedMode,
    .service_unavailable_action = RecoveryAction::kEnterDegradedMode,
  };
}

}  // namespace

int main() {
  namespace phm = openautosar::runtime::phm;

  phm::PlatformHealthManager manager;
  const auto invalid = manager.RegisterEntity({});
  Require(!invalid.HasValue(), "invalid PHM policy was accepted");

  const auto registered = manager.RegisterEntity(TestPolicy("ultrasonic-provider"));
  Require(registered.HasValue(), "valid PHM policy was rejected");
  Require(registered.Value().health == phm::HealthState::kHealthy, "initial PHM health changed");

  const auto duplicate = manager.RegisterEntity(TestPolicy("ultrasonic-provider"));
  Require(!duplicate.HasValue(), "duplicate PHM entity was accepted");

  const auto init = manager.ReportCheckpoint("ultrasonic-provider", "Init", 1'010U);
  const auto offered = manager.ReportCheckpoint("ultrasonic-provider", "ServiceOffered", 1'040U);
  const auto alive = manager.ReportCheckpoint("ultrasonic-provider", "Alive", 1'070U);
  Require(init.HasValue() && offered.HasValue() && alive.HasValue(), "checkpoint sequence failed");
  Require(alive.Value().status == phm::SupervisionStatus::kOk, "valid checkpoint was not OK");
  Require(alive.Value().last_checkpoint.value_or("") == "Alive", "last checkpoint changed");

  const auto alive_ok = manager.EvaluateAlive("ultrasonic-provider", 1'120U);
  Require(alive_ok.HasValue(), "alive evaluation failed");
  Require(
    alive_ok.Value().health == phm::HealthState::kHealthy,
    "fresh checkpoint was not healthy");

  const auto stale_degraded = manager.EvaluateAlive("ultrasonic-provider", 1'171U);
  Require(stale_degraded.HasValue(), "stale degraded evaluation failed");
  Require(
    stale_degraded.Value().status == phm::SupervisionStatus::kMissingCheckpoint,
    "missing alive checkpoint was not detected");
  Require(
    stale_degraded.Value().health == phm::HealthState::kDegraded,
    "stale entity not degraded");
  Require(
    stale_degraded.Value().action == phm::RecoveryAction::kEnterDegradedMode,
    "degraded recovery action changed");

  const auto stale_failed = manager.EvaluateAlive("ultrasonic-provider", 1'371U);
  Require(stale_failed.HasValue(), "stale failed evaluation failed");
  Require(stale_failed.Value().health == phm::HealthState::kFailed, "failed threshold not reached");
  Require(
    stale_failed.Value().action == phm::RecoveryAction::kRequestSafeState,
    "failed recovery action changed");

  const auto restarted = manager.ReportCheckpoint("ultrasonic-provider", "Init", 1'380U);
  Require(restarted.HasValue(), "checkpoint after stale state failed");
  Require(
    restarted.Value().health == phm::HealthState::kHealthy,
    "checkpoint did not restore health");

  const auto deadline_missed =
    manager.ReportCheckpoint("ultrasonic-provider", "ServiceOffered", 1'500U);
  Require(deadline_missed.HasValue(), "deadline miss report failed");
  Require(
    deadline_missed.Value().status == phm::SupervisionStatus::kDeadlineMissed,
    "deadline miss was not detected");
  Require(
    deadline_missed.Value().action == phm::RecoveryAction::kRestartProcess,
    "deadline recovery action changed");

  phm::PlatformHealthManager sequence_manager;
  Require(
    sequence_manager.RegisterEntity(TestPolicy("sequence-test")).HasValue(),
    "sequence setup failed");
  const auto wrong_sequence = sequence_manager.ReportCheckpoint("sequence-test", "Alive", 1'010U);
  Require(wrong_sequence.HasValue(), "wrong sequence report failed");
  Require(
    wrong_sequence.Value().status == phm::SupervisionStatus::kLogicalSequenceError,
    "wrong checkpoint sequence was not detected");

  const auto service_down = manager.ReportServiceAvailability("ultrasonic-provider", false, 1'510U);
  Require(service_down.HasValue(), "service availability report failed");
  Require(
    service_down.Value().status == phm::SupervisionStatus::kServiceUnavailable,
    "service unavailability was not detected");

  const auto resource = manager.ReportResourceExhaustion(
    "ultrasonic-provider",
    "cpu quota exceeded",
    1'520U);
  Require(resource.HasValue(), "resource exhaustion report failed");
  Require(
    resource.Value().status == phm::SupervisionStatus::kResourceExhausted,
    "resource exhaustion was not detected");

  const auto terminated = manager.ReportProcessTermination("ultrasonic-provider", 1'530U);
  Require(terminated.HasValue(), "process termination report failed");
  Require(
    terminated.Value().status == phm::SupervisionStatus::kProcessTerminated,
    "process termination was not detected");
  Require(
    terminated.Value().action == phm::RecoveryAction::kRestartProcess,
    "process termination recovery action changed");

  const auto first_restart = manager.ReportRestart("ultrasonic-provider", 1'540U);
  const auto second_restart = manager.ReportRestart("ultrasonic-provider", 1'550U);
  Require(first_restart.HasValue() && second_restart.HasValue(), "restart reports failed");
  Require(
    second_restart.Value().status == phm::SupervisionStatus::kRestartLimitExceeded,
    "restart limit was not detected");

  const auto all_reports = manager.EvaluateAll(1'560U);
  Require(all_reports.size() == 1U, "PHM evaluate-all report count changed");

  Require(
    phm::ToString(phm::HealthState::kDegraded) == std::string_view("Degraded"),
    "health text changed");
  Require(
    phm::ToString(phm::RecoveryAction::kRequestSafeState) == std::string_view("RequestSafeState"),
    "recovery action text changed");

  return 0;
}
