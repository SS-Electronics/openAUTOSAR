// SPDX-License-Identifier: MIT

#include "openautosar/runtime/vehicle_update_manager.h"

#include <algorithm>
#include <utility>

namespace openautosar::runtime::vucm {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"vehicle-update-management", message};
}

[[nodiscard]] bool IsNonEmpty(std::string_view value) noexcept {
  return !value.empty();
}

}  // namespace

VehicleUpdateManager::VehicleUpdateManager(VehicleUpdatePolicy policy)
  : policy_(std::move(policy)) {}

core::Result<bool> VehicleUpdateManager::RegisterEcu(EcuInventory inventory) {
  if (!IsNonEmpty(inventory.ecu_id) || !IsNonEmpty(inventory.machine_id)) {
    return core::Result<bool>::FromError(MakeError("ECU inventory record is incomplete"));
  }

  inventory_[inventory.ecu_id] = std::move(inventory);
  return core::Result<bool>::FromValue(true);
}

core::Result<VehicleCampaignPlan> VehicleUpdateManager::PlanCampaign(
  VehicleCampaignRequest request) {
  auto valid = ValidateRequest(request);
  if (!valid) {
    VehicleCampaignPlan rejected{
      .campaign_id = request.campaign_id,
      .state = CampaignState::kRejected,
      .steps = {},
      .audit = {},
    };
    active_campaign_ = rejected;
    Audit(CampaignState::kRejected, valid.Error().message);
    return core::Result<VehicleCampaignPlan>::FromError(valid.Error());
  }

  std::sort(
    request.packages.begin(),
    request.packages.end(),
    [](const auto& left, const auto& right) {
      if (left.ecu_id == right.ecu_id) {
        return left.package.cluster_name < right.package.cluster_name;
      }
      return left.ecu_id < right.ecu_id;
    });

  VehicleCampaignPlan plan{
    .campaign_id = request.campaign_id,
    .state = CampaignState::kPlanned,
    .steps = {},
    .audit = {},
  };
  std::uint32_t sequence{1U};
  for (const auto& item : request.packages) {
    plan.steps.push_back({
      .sequence = sequence++,
      .ecu_id = item.ecu_id,
      .package = item.package,
      .required_activation_state = item.required_activation_state,
      .state = StepState::kReady,
      .reason = "ready",
    });
  }

  active_campaign_ = plan;
  Audit(CampaignState::kPlanned, "vehicle update campaign planned");
  return core::Result<VehicleCampaignPlan>::FromValue(active_campaign_.value());
}

core::Result<VehicleCampaignPlan> VehicleUpdateManager::ActivateCampaign(
  std::string_view campaign_id,
  ucm::MachineState vehicle_state) {
  if (!active_campaign_.has_value() || active_campaign_->campaign_id != campaign_id) {
    return core::Result<VehicleCampaignPlan>::FromError(MakeError("campaign is not active"));
  }

  if (active_campaign_->state != CampaignState::kPlanned &&
      active_campaign_->state != CampaignState::kAwaitingActivation) {
    return core::Result<VehicleCampaignPlan>::FromError(
      MakeError("campaign cannot be activated from current state"));
  }

  for (const auto& step : active_campaign_->steps) {
    if (vehicle_state != step.required_activation_state) {
      active_campaign_->state = CampaignState::kRejected;
      Audit(
        CampaignState::kRejected,
        "vehicle activation precondition failed",
        step.ecu_id,
        step.package.cluster_name);
      return core::Result<VehicleCampaignPlan>::FromError(
        MakeError("vehicle activation precondition failed"));
    }
  }

  for (auto& step : active_campaign_->steps) {
    step.state = StepState::kActivated;
    step.reason = "activation requested";
    inventory_[step.ecu_id].availability = EcuAvailability::kUpdating;
  }

  active_campaign_->state = policy_.require_health_confirmation
                              ? CampaignState::kAwaitingHealthConfirmation
                              : CampaignState::kCommitted;
  Audit(active_campaign_->state, "vehicle update activation requested");
  if (!policy_.require_health_confirmation) {
    for (auto& step : active_campaign_->steps) {
      step.state = StepState::kCommitted;
      step.reason = "committed without health confirmation";
      inventory_[step.ecu_id].availability = EcuAvailability::kOnline;
      inventory_[step.ecu_id].installed_versions[step.package.cluster_name] =
        step.package.version;
    }
  }

  return core::Result<VehicleCampaignPlan>::FromValue(active_campaign_.value());
}

core::Result<VehicleCampaignPlan> VehicleUpdateManager::ConfirmEcuHealth(
  std::string_view campaign_id,
  std::string_view ecu_id,
  ucm::HealthConfirmation confirmation) {
  if (!active_campaign_.has_value() || active_campaign_->campaign_id != campaign_id) {
    return core::Result<VehicleCampaignPlan>::FromError(MakeError("campaign is not active"));
  }

  if (active_campaign_->state != CampaignState::kAwaitingHealthConfirmation) {
    return core::Result<VehicleCampaignPlan>::FromError(
      MakeError("campaign is not waiting for health confirmation"));
  }

  auto step = std::find_if(
    active_campaign_->steps.begin(),
    active_campaign_->steps.end(),
    [ecu_id](const CampaignStep& item) { return item.ecu_id == ecu_id; });
  if (step == active_campaign_->steps.end()) {
    return core::Result<VehicleCampaignPlan>::FromError(
      MakeError("ECU is not part of campaign"));
  }

  if (!HealthConfirmed(confirmation)) {
    for (auto& item : active_campaign_->steps) {
      item.state = StepState::kRolledBack;
      item.reason = item.ecu_id == ecu_id ? "health confirmation failed"
                                          : "coordinated rollback";
      inventory_[item.ecu_id].availability = EcuAvailability::kRecoveryRequired;
    }
    active_campaign_->state = CampaignState::kRolledBack;
    Audit(CampaignState::kRolledBack, "vehicle campaign rolled back", std::string(ecu_id));
    return core::Result<VehicleCampaignPlan>::FromValue(active_campaign_.value());
  }

  step->state = StepState::kCommitted;
  step->reason = "health confirmed";
  inventory_[step->ecu_id].availability = EcuAvailability::kOnline;
  inventory_[step->ecu_id].installed_versions[step->package.cluster_name] =
    step->package.version;
  Audit(
    CampaignState::kAwaitingHealthConfirmation,
    "ECU health confirmed",
    step->ecu_id,
    step->package.cluster_name);

  const bool all_committed = std::all_of(
    active_campaign_->steps.begin(),
    active_campaign_->steps.end(),
    [](const CampaignStep& item) { return item.state == StepState::kCommitted; });
  if (all_committed) {
    active_campaign_->state = CampaignState::kCommitted;
    Audit(CampaignState::kCommitted, "vehicle campaign committed");
  }

  return core::Result<VehicleCampaignPlan>::FromValue(active_campaign_.value());
}

core::Result<VehicleCampaignPlan> VehicleUpdateManager::AbortAndRollback(
  std::string_view campaign_id,
  std::string reason) {
  if (reason.empty()) {
    return core::Result<VehicleCampaignPlan>::FromError(MakeError("rollback reason is empty"));
  }

  if (!active_campaign_.has_value() || active_campaign_->campaign_id != campaign_id) {
    return core::Result<VehicleCampaignPlan>::FromError(MakeError("campaign is not active"));
  }

  for (auto& step : active_campaign_->steps) {
    step.state = StepState::kRolledBack;
    step.reason = reason;
    inventory_[step.ecu_id].availability = EcuAvailability::kRecoveryRequired;
  }
  active_campaign_->state = CampaignState::kRolledBack;
  Audit(CampaignState::kRolledBack, std::move(reason));
  return core::Result<VehicleCampaignPlan>::FromValue(active_campaign_.value());
}

std::optional<EcuInventory> VehicleUpdateManager::InventoryFor(
  std::string_view ecu_id) const {
  const auto iter = inventory_.find(std::string(ecu_id));
  if (iter == inventory_.end()) {
    return std::nullopt;
  }

  return iter->second;
}

core::Result<bool> VehicleUpdateManager::ValidateRequest(
  const VehicleCampaignRequest& request) const {
  if (!IsNonEmpty(request.campaign_id)) {
    return core::Result<bool>::FromError(MakeError("vehicle campaign id is empty"));
  }

  if (request.packages.empty()) {
    return core::Result<bool>::FromError(MakeError("vehicle campaign has no packages"));
  }

  if (request.packages.size() > policy_.max_steps) {
    return core::Result<bool>::FromError(MakeError("vehicle campaign step budget exceeded"));
  }

  std::vector<std::string> targets;
  for (const auto& item : request.packages) {
    auto valid = ValidatePackage(item);
    if (!valid) {
      return valid;
    }

    const auto target = item.ecu_id + ":" + item.package.cluster_name;
    if (std::find(targets.begin(), targets.end(), target) != targets.end()) {
      return core::Result<bool>::FromError(
        MakeError("campaign contains duplicate cluster target"));
    }
    targets.push_back(target);
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> VehicleUpdateManager::ValidatePackage(
  const CampaignPackage& item) const {
  if (!IsNonEmpty(item.ecu_id) || !IsNonEmpty(item.package.cluster_name) ||
      !IsNonEmpty(item.package.version)) {
    return core::Result<bool>::FromError(MakeError("campaign package metadata is incomplete"));
  }

  const auto ecu = inventory_.find(item.ecu_id);
  if (ecu == inventory_.end()) {
    return core::Result<bool>::FromError(MakeError("campaign references unknown ECU"));
  }

  if (policy_.require_all_targets_online &&
      ecu->second.availability != EcuAvailability::kOnline) {
    return core::Result<bool>::FromError(MakeError("campaign target ECU is not online"));
  }

  if (policy_.require_rollback_capable && !ecu->second.rollback_supported) {
    return core::Result<bool>::FromError(MakeError("campaign target cannot roll back"));
  }

  if (!item.package.signature_verified || !IsTrustedSigner(item.package.signer_id)) {
    return core::Result<bool>::FromError(MakeError("campaign package signer is not trusted"));
  }

  if (!item.package.compatible_platform || !item.package.manifests_present) {
    return core::Result<bool>::FromError(MakeError("campaign package is not deployable"));
  }

  if (policy_.require_sbom && !item.package.sbom_present) {
    return core::Result<bool>::FromError(MakeError("campaign package SBOM is missing"));
  }

  if (policy_.require_provenance && !item.package.provenance_present) {
    return core::Result<bool>::FromError(MakeError("campaign package provenance is missing"));
  }

  if (item.package.required_free_bytes > ecu->second.inactive_slot_free_bytes) {
    return core::Result<bool>::FromError(MakeError("campaign package exceeds inactive slot"));
  }

  if (!AreDependenciesInstalled(item)) {
    return core::Result<bool>::FromError(MakeError("campaign dependency closure is incomplete"));
  }

  if (!IsVersionUpgrade(item)) {
    return core::Result<bool>::FromError(MakeError("campaign package version is not an upgrade"));
  }

  return core::Result<bool>::FromValue(true);
}

bool VehicleUpdateManager::IsTrustedSigner(std::string_view signer_id) const noexcept {
  return std::find(policy_.trusted_signers.begin(), policy_.trusted_signers.end(), signer_id) !=
         policy_.trusted_signers.end();
}

bool VehicleUpdateManager::IsVersionUpgrade(const CampaignPackage& item) const noexcept {
  const auto ecu = inventory_.find(item.ecu_id);
  if (ecu == inventory_.end()) {
    return false;
  }

  const auto version = ecu->second.installed_versions.find(item.package.cluster_name);
  if (version == ecu->second.installed_versions.end()) {
    return true;
  }

  return item.package.version > version->second;
}

bool VehicleUpdateManager::AreDependenciesInstalled(const CampaignPackage& item) const noexcept {
  const auto ecu = inventory_.find(item.ecu_id);
  if (ecu == inventory_.end()) {
    return false;
  }

  return std::all_of(
    item.package.dependencies.begin(),
    item.package.dependencies.end(),
    [&ecu](const std::string& dependency) {
      return ecu->second.installed_versions.find(dependency) !=
             ecu->second.installed_versions.end();
    });
}

bool VehicleUpdateManager::HealthConfirmed(
  const ucm::HealthConfirmation& confirmation) const noexcept {
  return confirmation.execution_started &&
         confirmation.platform_health == phm::HealthState::kHealthy &&
         confirmation.diagnostics_available &&
         confirmation.dashboard_available;
}

void VehicleUpdateManager::Audit(
  CampaignState state,
  std::string message,
  std::string ecu_id,
  std::string cluster_name) {
  if (!active_campaign_.has_value()) {
    active_campaign_ = VehicleCampaignPlan{};
  }

  active_campaign_->audit.push_back({
    .state = state,
    .ecu_id = std::move(ecu_id),
    .cluster_name = std::move(cluster_name),
    .message = std::move(message),
  });
}

std::string_view ToString(EcuAvailability availability) noexcept {
  switch (availability) {
    case EcuAvailability::kOnline:
      return "Online";
    case EcuAvailability::kOffline:
      return "Offline";
    case EcuAvailability::kUpdating:
      return "Updating";
    case EcuAvailability::kRecoveryRequired:
      return "RecoveryRequired";
  }

  return "Unknown";
}

std::string_view ToString(CampaignState state) noexcept {
  switch (state) {
    case CampaignState::kIdle:
      return "Idle";
    case CampaignState::kPlanned:
      return "Planned";
    case CampaignState::kAwaitingActivation:
      return "AwaitingActivation";
    case CampaignState::kAwaitingHealthConfirmation:
      return "AwaitingHealthConfirmation";
    case CampaignState::kCommitted:
      return "Committed";
    case CampaignState::kRolledBack:
      return "RolledBack";
    case CampaignState::kRejected:
      return "Rejected";
    case CampaignState::kFailed:
      return "Failed";
  }

  return "Unknown";
}

std::string_view ToString(StepState state) noexcept {
  switch (state) {
    case StepState::kPending:
      return "Pending";
    case StepState::kReady:
      return "Ready";
    case StepState::kActivated:
      return "Activated";
    case StepState::kCommitted:
      return "Committed";
    case StepState::kRolledBack:
      return "RolledBack";
    case StepState::kBlocked:
      return "Blocked";
    case StepState::kFailed:
      return "Failed";
  }

  return "Unknown";
}

}  // namespace openautosar::runtime::vucm
