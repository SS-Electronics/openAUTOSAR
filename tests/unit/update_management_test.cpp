// SPDX-License-Identifier: MIT

#include "openautosar/runtime/update_manager.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

openautosar::runtime::ucm::SoftwareClusterPackage ActivePackage() {
  return {
    .cluster_name = "ultrasonic-provider",
    .version = "1.0.0",
    .signer_id = "openautosar-dev",
    .signature_verified = true,
    .compatible_platform = true,
    .required_free_bytes = 0U,
    .dependencies = {},
    .manifests_present = true,
    .sbom_present = true,
    .provenance_present = true,
  };
}

openautosar::runtime::ucm::SoftwareClusterPackage UpdatePackage() {
  return {
    .cluster_name = "ultrasonic-provider",
    .version = "1.1.0",
    .signer_id = "openautosar-dev",
    .signature_verified = true,
    .compatible_platform = true,
    .required_free_bytes = 128U,
    .dependencies = {"platform-runtime"},
    .manifests_present = true,
    .sbom_present = true,
    .provenance_present = true,
  };
}

openautosar::runtime::ucm::UpdatePolicy Policy() {
  return {
    .activation_state = openautosar::runtime::ucm::MachineState::kUpdateAllowed,
    .max_boot_attempts = 2U,
    .require_sbom = true,
    .require_provenance = true,
    .trusted_signers = {"openautosar-dev"},
    .installed_versions = {
      {"platform-runtime", "1.0.0"},
      {"ultrasonic-provider", "1.0.0"},
    },
  };
}

openautosar::runtime::ucm::HealthConfirmation HealthyConfirmation() {
  return {
    .execution_started = true,
    .platform_health = openautosar::runtime::phm::HealthState::kHealthy,
    .diagnostics_available = true,
    .dashboard_available = true,
  };
}

}  // namespace

int main() {
  namespace ucm = openautosar::runtime::ucm;

  ucm::RaucBackendSimulator backend(1024U, ActivePackage());
  ucm::UpdateManager manager(backend, Policy());

  Require(manager.State() == ucm::TransactionState::kIdle, "UCM initial state changed");
  Require(!manager.AuditTrail().empty(), "UCM audit trail did not record initialization");

  auto transferred = manager.TransferPackage(UpdatePackage());
  Require(transferred.HasValue(), "valid package transfer failed");
  Require(transferred.Value() == ucm::TransactionState::kTransferred, "transfer state changed");

  auto verified = manager.VerifyPackage();
  Require(verified.HasValue(), "valid package verification failed");
  Require(verified.Value() == ucm::TransactionState::kVerified, "verify state changed");

  auto staged = manager.StagePackage();
  Require(staged.HasValue(), "valid package staging failed");
  Require(staged.Value() == ucm::TransactionState::kStaged, "stage state changed");

  auto status_after_stage = backend.Status();
  Require(status_after_stage.HasValue(), "backend status after stage failed");
  Require(
    status_after_stage.Value().installed_in_inactive_slot.has_value(),
    "inactive slot does not contain staged package");

  auto denied_activation = manager.Activate(ucm::MachineState::kDrivingReady);
  Require(!denied_activation.HasValue(), "activation in DrivingReady was accepted");
  Require(
    manager.State() == ucm::TransactionState::kRejected,
    "precondition rejection state changed");

  ucm::RaucBackendSimulator success_backend(1024U, ActivePackage());
  ucm::UpdateManager success(success_backend, Policy());
  Require(success.TransferPackage(UpdatePackage()).HasValue(), "success transfer failed");
  Require(success.VerifyPackage().HasValue(), "success verify failed");
  Require(success.StagePackage().HasValue(), "success stage failed");
  Require(
    success.Activate(ucm::MachineState::kUpdateAllowed).HasValue(),
    "success activation failed");
  Require(
    success.State() == ucm::TransactionState::kPendingHealthConfirmation,
    "pending-health state changed");

  auto status_pending = success_backend.Status();
  Require(status_pending.HasValue(), "pending backend status failed");
  Require(status_pending.Value().pending_slot.has_value(), "pending slot was not marked");
  Require(status_pending.Value().boot_attempts_remaining == 2U, "boot attempt budget changed");

  auto committed = success.ConfirmHealth(HealthyConfirmation());
  Require(committed.HasValue(), "healthy confirmation commit failed");
  Require(committed.Value() == ucm::TransactionState::kCommitted, "commit state changed");
  auto status_committed = success_backend.Status();
  Require(status_committed.HasValue(), "committed backend status failed");
  Require(!status_committed.Value().pending_slot.has_value(), "pending slot remained after commit");
  Require(
    status_committed.Value().active_package->version == "1.1.0",
    "committed active package version changed");

  ucm::RaucBackendSimulator rollback_backend(1024U, ActivePackage());
  ucm::UpdateManager rollback(rollback_backend, Policy());
  Require(rollback.TransferPackage(UpdatePackage()).HasValue(), "rollback transfer failed");
  Require(rollback.VerifyPackage().HasValue(), "rollback verify failed");
  Require(rollback.StagePackage().HasValue(), "rollback stage failed");
  Require(
    rollback.Activate(ucm::MachineState::kUpdateAllowed).HasValue(),
    "rollback activate failed");
  auto failed_health = HealthyConfirmation();
  failed_health.platform_health = openautosar::runtime::phm::HealthState::kFailed;
  auto rolled_back = rollback.ConfirmHealth(failed_health);
  Require(rolled_back.HasValue(), "failed health rollback failed");
  Require(rolled_back.Value() == ucm::TransactionState::kRolledBack, "rollback state changed");
  auto status_rolled_back = rollback_backend.Status();
  Require(status_rolled_back.HasValue(), "rollback backend status failed");
  Require(
    !status_rolled_back.Value().pending_slot.has_value(),
    "pending slot remained after rollback");
  Require(status_rolled_back.Value().active_slot == "A", "rollback did not restore previous slot");

  ucm::RaucBackendSimulator reject_backend(64U, ActivePackage());
  ucm::UpdateManager reject(reject_backend, Policy());
  auto too_large = UpdatePackage();
  too_large.required_free_bytes = 128U;
  Require(reject.TransferPackage(too_large).HasValue(), "reject transfer failed");
  Require(!reject.VerifyPackage().HasValue(), "oversized package was verified");
  Require(reject.State() == ucm::TransactionState::kRejected, "oversized package state changed");

  ucm::RaucBackendSimulator signer_backend(1024U, ActivePackage());
  ucm::UpdateManager signer(signer_backend, Policy());
  auto untrusted = UpdatePackage();
  untrusted.signer_id = "unknown";
  Require(signer.TransferPackage(untrusted).HasValue(), "untrusted transfer failed");
  Require(!signer.VerifyPackage().HasValue(), "untrusted signer was verified");

  ucm::RaucBackendSimulator dependency_backend(1024U, ActivePackage());
  ucm::UpdatePolicy dependency_policy = Policy();
  dependency_policy.installed_versions.erase("platform-runtime");
  ucm::UpdateManager dependency(dependency_backend, dependency_policy);
  Require(dependency.TransferPackage(UpdatePackage()).HasValue(), "dependency transfer failed");
  Require(!dependency.VerifyPackage().HasValue(), "missing dependency was verified");

  ucm::RaucBackendSimulator failure_backend(1024U, ActivePackage());
  failure_backend.FailNext(ucm::BackendOperation::kInstallInactive);
  ucm::UpdateManager failure(failure_backend, Policy());
  Require(failure.TransferPackage(UpdatePackage()).HasValue(), "failure transfer failed");
  Require(failure.VerifyPackage().HasValue(), "failure verify failed");
  Require(!failure.StagePackage().HasValue(), "backend install failure was ignored");
  Require(failure.State() == ucm::TransactionState::kFailed, "backend failure state changed");

  Require(
    ucm::ToString(ucm::TransactionState::kCommitted) == std::string_view("Committed"),
    "transaction state text changed");
  Require(
    ucm::ToString(ucm::MachineState::kUpdateAllowed) == std::string_view("UpdateAllowed"),
    "machine state text changed");
  Require(
    ucm::ToString(ucm::BackendOperation::kRollback) == std::string_view("Rollback"),
    "backend operation text changed");

  return 0;
}
