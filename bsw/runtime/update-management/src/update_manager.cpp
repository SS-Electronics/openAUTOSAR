// SPDX-License-Identifier: MIT

#include "openautosar/runtime/update_manager.h"

#include <algorithm>
#include <utility>

namespace openautosar::runtime::ucm {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"update-management", message};
}

[[nodiscard]] bool IsNonEmptyText(std::string_view value) noexcept {
  return !value.empty();
}

}  // namespace

RaucBackendSimulator::RaucBackendSimulator(
  std::size_t inactive_slot_free_bytes,
  SoftwareClusterPackage active_package) {
  status_.active_slot = "A";
  status_.inactive_slot = "B";
  status_.active_package = std::move(active_package);
  status_.inactive_slot_free_bytes = inactive_slot_free_bytes;
}

core::Result<SlotStatus> RaucBackendSimulator::Status() const {
  return core::Result<SlotStatus>::FromValue(status_);
}

core::Result<bool> RaucBackendSimulator::InstallInactive(const SoftwareClusterPackage& package) {
  auto failure = ConsumeFailure(BackendOperation::kInstallInactive);
  if (!failure) {
    return core::Result<bool>::FromError(failure.Error());
  }

  if (package.required_free_bytes > status_.inactive_slot_free_bytes) {
    return core::Result<bool>::FromError(
      MakeError("inactive slot does not have enough free space"));
  }

  if (status_.pending_slot.has_value()) {
    return core::Result<bool>::FromError(MakeError("inactive slot already has a pending update"));
  }

  status_.installed_in_inactive_slot = package;
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> RaucBackendSimulator::ActivatePending(std::uint32_t boot_attempts) {
  auto failure = ConsumeFailure(BackendOperation::kActivatePending);
  if (!failure) {
    return core::Result<bool>::FromError(failure.Error());
  }

  if (!status_.installed_in_inactive_slot.has_value()) {
    return core::Result<bool>::FromError(MakeError("no update is installed in inactive slot"));
  }

  if (boot_attempts == 0U) {
    return core::Result<bool>::FromError(MakeError("boot attempt budget is zero"));
  }

  previous_active_package_ = status_.active_package;
  SwapSlots();
  status_.active_package = status_.installed_in_inactive_slot;
  status_.pending_slot = status_.active_slot;
  status_.boot_attempts_remaining = boot_attempts;
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> RaucBackendSimulator::Commit() {
  auto failure = ConsumeFailure(BackendOperation::kCommit);
  if (!failure) {
    return core::Result<bool>::FromError(failure.Error());
  }

  if (!status_.pending_slot.has_value()) {
    return core::Result<bool>::FromError(MakeError("no pending slot can be committed"));
  }

  status_.pending_slot = std::nullopt;
  status_.installed_in_inactive_slot = std::nullopt;
  previous_active_package_ = std::nullopt;
  status_.boot_attempts_remaining = 0U;
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> RaucBackendSimulator::Rollback() {
  auto failure = ConsumeFailure(BackendOperation::kRollback);
  if (!failure) {
    return core::Result<bool>::FromError(failure.Error());
  }

  if (!status_.pending_slot.has_value()) {
    status_.installed_in_inactive_slot = std::nullopt;
    return core::Result<bool>::FromValue(true);
  }

  const auto failed_package = status_.active_package;
  SwapSlots();
  status_.installed_in_inactive_slot = failed_package;
  status_.active_package = previous_active_package_;
  previous_active_package_ = std::nullopt;
  status_.pending_slot = std::nullopt;
  status_.boot_attempts_remaining = 0U;
  return core::Result<bool>::FromValue(true);
}

void RaucBackendSimulator::FailNext(BackendOperation operation) noexcept { fail_next_ = operation; }

core::Result<bool> RaucBackendSimulator::ConsumeFailure(BackendOperation operation) {
  if (fail_next_ != operation) {
    return core::Result<bool>::FromValue(true);
  }

  fail_next_ = std::nullopt;
  return core::Result<bool>::FromError(MakeError("simulated RAUC backend failure"));
}

void RaucBackendSimulator::SwapSlots() {
  std::swap(status_.active_slot, status_.inactive_slot);
}

UpdateManager::UpdateManager(IUpdateBackend& backend, UpdatePolicy policy)
  : backend_(backend), policy_(std::move(policy)) {
  Audit(TransactionState::kIdle, "UCM initialized");
}

core::Result<TransactionState> UpdateManager::TransferPackage(SoftwareClusterPackage package) {
  if (state_ != TransactionState::kIdle && state_ != TransactionState::kCommitted &&
      state_ != TransactionState::kRolledBack && state_ != TransactionState::kRejected) {
    return core::Result<TransactionState>::FromError(
      MakeError("cannot transfer package while transaction is active"));
  }

  if (!IsNonEmptyText(package.cluster_name) || !IsNonEmptyText(package.version)) {
    state_ = TransactionState::kRejected;
    Audit(state_, "package metadata is incomplete");
    return core::Result<TransactionState>::FromError(MakeError("package metadata is incomplete"));
  }

  pending_package_ = std::move(package);
  state_ = TransactionState::kTransferred;
  Audit(state_, "package transferred");
  return core::Result<TransactionState>::FromValue(state_);
}

core::Result<TransactionState> UpdateManager::VerifyPackage() {
  if (state_ != TransactionState::kTransferred || !pending_package_.has_value()) {
    return core::Result<TransactionState>::FromError(MakeError("no transferred package to verify"));
  }

  auto validation = ValidatePackage(pending_package_.value());
  if (!validation) {
    state_ = TransactionState::kRejected;
    Audit(state_, validation.Error().message);
    return core::Result<TransactionState>::FromError(validation.Error());
  }

  state_ = TransactionState::kVerified;
  Audit(state_, "package verified");
  return core::Result<TransactionState>::FromValue(state_);
}

core::Result<TransactionState> UpdateManager::StagePackage() {
  if (state_ != TransactionState::kVerified || !pending_package_.has_value()) {
    return core::Result<TransactionState>::FromError(MakeError("package is not verified"));
  }

  auto installed = backend_.InstallInactive(pending_package_.value());
  if (!installed) {
    state_ = TransactionState::kFailed;
    Audit(state_, installed.Error().message, BackendOperation::kInstallInactive);
    return core::Result<TransactionState>::FromError(installed.Error());
  }

  state_ = TransactionState::kStaged;
  Audit(state_, "package staged in inactive slot", BackendOperation::kInstallInactive);
  return core::Result<TransactionState>::FromValue(state_);
}

core::Result<TransactionState> UpdateManager::Activate(MachineState machine_state) {
  if (state_ != TransactionState::kStaged) {
    return core::Result<TransactionState>::FromError(MakeError("package is not staged"));
  }

  if (machine_state != policy_.activation_state) {
    state_ = TransactionState::kRejected;
    Audit(state_, "activation precondition failed");
    return core::Result<TransactionState>::FromError(MakeError("activation precondition failed"));
  }

  auto activated = backend_.ActivatePending(policy_.max_boot_attempts);
  if (!activated) {
    state_ = TransactionState::kFailed;
    Audit(state_, activated.Error().message, BackendOperation::kActivatePending);
    return core::Result<TransactionState>::FromError(activated.Error());
  }

  state_ = TransactionState::kPendingHealthConfirmation;
  Audit(state_, "pending slot activated", BackendOperation::kActivatePending);
  return core::Result<TransactionState>::FromValue(state_);
}

core::Result<TransactionState> UpdateManager::ConfirmHealth(HealthConfirmation confirmation) {
  if (state_ != TransactionState::kPendingHealthConfirmation) {
    return core::Result<TransactionState>::FromError(
      MakeError("transaction is not waiting for health confirmation"));
  }

  if (HealthConfirmed(confirmation)) {
    auto committed = backend_.Commit();
    if (!committed) {
      state_ = TransactionState::kFailed;
      Audit(state_, committed.Error().message, BackendOperation::kCommit);
      return core::Result<TransactionState>::FromError(committed.Error());
    }

    if (pending_package_.has_value()) {
      policy_.installed_versions[pending_package_->cluster_name] = pending_package_->version;
    }
    state_ = TransactionState::kCommitted;
    Audit(state_, "health confirmed and pending slot committed", BackendOperation::kCommit);
    return core::Result<TransactionState>::FromValue(state_);
  }

  auto rolled_back = backend_.Rollback();
  if (!rolled_back) {
    state_ = TransactionState::kFailed;
    Audit(state_, rolled_back.Error().message, BackendOperation::kRollback);
    return core::Result<TransactionState>::FromError(rolled_back.Error());
  }

  state_ = TransactionState::kRolledBack;
  Audit(state_, "health confirmation failed and rollback completed", BackendOperation::kRollback);
  return core::Result<TransactionState>::FromValue(state_);
}

core::Result<TransactionState> UpdateManager::AbortAndRollback(std::string reason) {
  if (reason.empty()) {
    return core::Result<TransactionState>::FromError(MakeError("rollback reason is empty"));
  }

  auto rolled_back = backend_.Rollback();
  if (!rolled_back) {
    state_ = TransactionState::kFailed;
    Audit(state_, rolled_back.Error().message, BackendOperation::kRollback);
    return core::Result<TransactionState>::FromError(rolled_back.Error());
  }

  state_ = TransactionState::kRolledBack;
  Audit(state_, std::move(reason), BackendOperation::kRollback);
  return core::Result<TransactionState>::FromValue(state_);
}

core::Result<bool> UpdateManager::ValidatePackage(const SoftwareClusterPackage& package) const {
  if (!package.signature_verified || !IsTrustedSigner(package.signer_id)) {
    return core::Result<bool>::FromError(MakeError("package signature or signer is not trusted"));
  }

  if (!package.compatible_platform) {
    return core::Result<bool>::FromError(MakeError("package is not compatible with this platform"));
  }

  if (!package.manifests_present) {
    return core::Result<bool>::FromError(MakeError("package manifests are missing"));
  }

  if (policy_.require_sbom && !package.sbom_present) {
    return core::Result<bool>::FromError(MakeError("package SBOM is missing"));
  }

  if (policy_.require_provenance && !package.provenance_present) {
    return core::Result<bool>::FromError(MakeError("package provenance is missing"));
  }

  if (!AreDependenciesInstalled(package)) {
    return core::Result<bool>::FromError(MakeError("package dependency closure is incomplete"));
  }

  if (!IsVersionUpgrade(package)) {
    return core::Result<bool>::FromError(MakeError("package version is not an upgrade"));
  }

  auto status = backend_.Status();
  if (!status) {
    return core::Result<bool>::FromError(status.Error());
  }

  if (package.required_free_bytes > status.Value().inactive_slot_free_bytes) {
    return core::Result<bool>::FromError(
      MakeError("package requires more space than inactive slot"));
  }

  return core::Result<bool>::FromValue(true);
}

bool UpdateManager::IsTrustedSigner(std::string_view signer_id) const noexcept {
  return std::find(policy_.trusted_signers.begin(), policy_.trusted_signers.end(), signer_id) !=
         policy_.trusted_signers.end();
}

bool UpdateManager::AreDependenciesInstalled(
  const SoftwareClusterPackage& package) const noexcept {
  return std::all_of(
    package.dependencies.begin(),
    package.dependencies.end(),
    [this](const auto& name) {
      return policy_.installed_versions.find(name) != policy_.installed_versions.end();
    });
}

bool UpdateManager::IsVersionUpgrade(const SoftwareClusterPackage& package) const noexcept {
  const auto iter = policy_.installed_versions.find(package.cluster_name);
  if (iter == policy_.installed_versions.end()) {
    return true;
  }

  return package.version > iter->second;
}

bool UpdateManager::HealthConfirmed(const HealthConfirmation& confirmation) noexcept {
  return confirmation.execution_started &&
         confirmation.platform_health == phm::HealthState::kHealthy &&
         confirmation.diagnostics_available && confirmation.dashboard_available;
}

void UpdateManager::Audit(
  TransactionState state,
  std::string message,
  std::optional<BackendOperation> operation) {
  audit_.push_back({
    .state = state,
    .message = std::move(message),
    .backend_operation = operation,
  });
}

std::string_view ToString(MachineState state) noexcept {
  switch (state) {
    case MachineState::kStartup:
      return "Startup";
    case MachineState::kDrivingReady:
      return "DrivingReady";
    case MachineState::kUpdateAllowed:
      return "UpdateAllowed";
    case MachineState::kDegraded:
      return "Degraded";
    case MachineState::kShutdown:
      return "Shutdown";
  }

  return "Unknown";
}

std::string_view ToString(TransactionState state) noexcept {
  switch (state) {
    case TransactionState::kIdle:
      return "Idle";
    case TransactionState::kTransferred:
      return "Transferred";
    case TransactionState::kVerified:
      return "Verified";
    case TransactionState::kStaged:
      return "Staged";
    case TransactionState::kPendingActivation:
      return "PendingActivation";
    case TransactionState::kPendingHealthConfirmation:
      return "PendingHealthConfirmation";
    case TransactionState::kCommitted:
      return "Committed";
    case TransactionState::kRolledBack:
      return "RolledBack";
    case TransactionState::kRejected:
      return "Rejected";
    case TransactionState::kFailed:
      return "Failed";
  }

  return "Unknown";
}

std::string_view ToString(BackendOperation operation) noexcept {
  switch (operation) {
    case BackendOperation::kNone:
      return "None";
    case BackendOperation::kInstallInactive:
      return "InstallInactive";
    case BackendOperation::kActivatePending:
      return "ActivatePending";
    case BackendOperation::kCommit:
      return "Commit";
    case BackendOperation::kRollback:
      return "Rollback";
  }

  return "Unknown";
}

}  // namespace openautosar::runtime::ucm
