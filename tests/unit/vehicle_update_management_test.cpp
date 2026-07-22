// SPDX-License-Identifier: MIT

#include "openautosar/runtime/vehicle_update_manager.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

openautosar::runtime::vucm::VehicleUpdatePolicy Policy() {
  return {
    .max_steps = 4U,
    .require_all_targets_online = true,
    .require_rollback_capable = true,
    .require_sbom = true,
    .require_provenance = true,
    .require_health_confirmation = true,
    .trusted_signers = {"openautosar-dev"},
  };
}

openautosar::runtime::vucm::EcuInventory Ecu(
  std::string_view ecu_id,
  std::size_t free_bytes = 1024U) {
  return {
    .ecu_id = std::string(ecu_id),
    .machine_id = "qemux86-64",
    .availability = openautosar::runtime::vucm::EcuAvailability::kOnline,
    .machine_state = openautosar::runtime::ucm::MachineState::kUpdateAllowed,
    .rollback_supported = true,
    .inactive_slot_free_bytes = free_bytes,
    .installed_versions = {
      {"platform-runtime", "1.0.0"},
      {"ultrasonic-provider", "1.0.0"},
      {"dashboard", "1.0.0"},
    },
  };
}

openautosar::runtime::ucm::SoftwareClusterPackage Package(
  std::string_view cluster_name,
  std::string_view version) {
  return {
    .cluster_name = std::string(cluster_name),
    .version = std::string(version),
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

openautosar::runtime::vucm::VehicleCampaignRequest Campaign() {
  namespace vucm = openautosar::runtime::vucm;
  return {
    .campaign_id = "campaign-ultrasonic-1",
    .packages = {
      {
        .ecu_id = "adaptive-qemu",
        .package = Package("ultrasonic-provider", "1.1.0"),
        .required_activation_state = openautosar::runtime::ucm::MachineState::kUpdateAllowed,
      },
      {
        .ecu_id = "dashboard-qemu",
        .package = Package("dashboard", "1.1.0"),
        .required_activation_state = openautosar::runtime::ucm::MachineState::kUpdateAllowed,
      },
    },
  };
}

openautosar::runtime::ucm::HealthConfirmation Healthy() {
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
  namespace vucm = openautosar::runtime::vucm;

  vucm::VehicleUpdateManager manager{Policy()};
  Require(!manager.RegisterEcu({}).HasValue(), "incomplete ECU inventory was accepted");
  Require(manager.RegisterEcu(Ecu("adaptive-qemu")).HasValue(), "adaptive ECU register failed");
  Require(manager.RegisterEcu(Ecu("dashboard-qemu")).HasValue(), "dashboard ECU register failed");

  auto plan = manager.PlanCampaign(Campaign());
  Require(plan.HasValue(), "valid vehicle campaign did not plan");
  Require(plan.Value().state == vucm::CampaignState::kPlanned, "campaign plan state changed");
  Require(plan.Value().steps.size() == 2U, "campaign step count changed");
  Require(plan.Value().steps[0U].ecu_id == "adaptive-qemu", "campaign ordering changed");
  Require(plan.Value().steps[0U].state == vucm::StepState::kReady, "step ready state changed");
  Require(!plan.Value().audit.empty(), "campaign planning did not audit");

  auto denied = manager.ActivateCampaign(
    "campaign-ultrasonic-1",
    ucm::MachineState::kDrivingReady);
  Require(!denied.HasValue(), "vehicle campaign activated while driving");

  vucm::VehicleUpdateManager success{Policy()};
  Require(success.RegisterEcu(Ecu("adaptive-qemu")).HasValue(), "success ECU register failed");
  Require(success.RegisterEcu(Ecu("dashboard-qemu")).HasValue(), "success ECU2 register failed");
  Require(success.PlanCampaign(Campaign()).HasValue(), "success campaign plan failed");
  auto activated = success.ActivateCampaign(
    "campaign-ultrasonic-1",
    ucm::MachineState::kUpdateAllowed);
  Require(activated.HasValue(), "vehicle campaign activation failed");
  Require(
    activated.Value().state == vucm::CampaignState::kAwaitingHealthConfirmation,
    "campaign did not wait for health");
  Require(
    success.InventoryFor("adaptive-qemu")->availability == vucm::EcuAvailability::kUpdating,
    "activated ECU was not marked updating");

  auto first_health = success.ConfirmEcuHealth(
    "campaign-ultrasonic-1",
    "adaptive-qemu",
    Healthy());
  Require(first_health.HasValue(), "first ECU health confirmation failed");
  Require(
    first_health.Value().state == vucm::CampaignState::kAwaitingHealthConfirmation,
    "campaign committed before all ECUs confirmed");
  auto second_health = success.ConfirmEcuHealth(
    "campaign-ultrasonic-1",
    "dashboard-qemu",
    Healthy());
  Require(second_health.HasValue(), "second ECU health confirmation failed");
  Require(second_health.Value().state == vucm::CampaignState::kCommitted, "campaign commit failed");
  Require(
    success.InventoryFor("adaptive-qemu")->installed_versions["ultrasonic-provider"] == "1.1.0",
    "adaptive ECU installed version was not updated");
  Require(
    success.InventoryFor("dashboard-qemu")->installed_versions["dashboard"] == "1.1.0",
    "dashboard ECU installed version was not updated");

  vucm::VehicleUpdateManager rollback{Policy()};
  Require(rollback.RegisterEcu(Ecu("adaptive-qemu")).HasValue(), "rollback ECU register failed");
  Require(rollback.RegisterEcu(Ecu("dashboard-qemu")).HasValue(), "rollback ECU2 register failed");
  Require(rollback.PlanCampaign(Campaign()).HasValue(), "rollback campaign plan failed");
  Require(
    rollback.ActivateCampaign("campaign-ultrasonic-1", ucm::MachineState::kUpdateAllowed)
      .HasValue(),
    "rollback activation failed");
  auto failed = Healthy();
  failed.dashboard_available = false;
  auto rolled_back = rollback.ConfirmEcuHealth(
    "campaign-ultrasonic-1",
    "adaptive-qemu",
    failed);
  Require(rolled_back.HasValue(), "vehicle rollback after health failure failed");
  Require(
    rolled_back.Value().state == vucm::CampaignState::kRolledBack,
    "vehicle campaign did not roll back");
  Require(
    rollback.InventoryFor("adaptive-qemu")->availability ==
      vucm::EcuAvailability::kRecoveryRequired,
    "failed ECU was not marked for recovery");

  vucm::VehicleUpdateManager reject{Policy()};
  Require(reject.RegisterEcu(Ecu("adaptive-qemu", 64U)).HasValue(), "reject ECU register failed");
  auto too_large = Campaign();
  too_large.packages.resize(1U);
  Require(!reject.PlanCampaign(too_large).HasValue(), "oversized vehicle package was planned");

  vucm::VehicleUpdateManager signer{Policy()};
  Require(signer.RegisterEcu(Ecu("adaptive-qemu")).HasValue(), "signer ECU register failed");
  auto untrusted = Campaign();
  untrusted.packages.resize(1U);
  untrusted.packages[0U].package.signer_id = "unknown";
  Require(!signer.PlanCampaign(untrusted).HasValue(), "untrusted vehicle package was planned");

  vucm::VehicleUpdateManager dependency{Policy()};
  auto dependency_ecu = Ecu("adaptive-qemu");
  dependency_ecu.installed_versions.erase("platform-runtime");
  Require(dependency.RegisterEcu(dependency_ecu).HasValue(), "dependency ECU register failed");
  auto missing_dependency = Campaign();
  missing_dependency.packages.resize(1U);
  Require(
    !dependency.PlanCampaign(missing_dependency).HasValue(),
    "missing vehicle dependency was planned");

  vucm::VehicleUpdateManager duplicate{Policy()};
  Require(duplicate.RegisterEcu(Ecu("adaptive-qemu")).HasValue(), "duplicate ECU register failed");
  auto duplicate_campaign = Campaign();
  duplicate_campaign.packages.resize(2U);
  duplicate_campaign.packages[1U] = duplicate_campaign.packages[0U];
  Require(!duplicate.PlanCampaign(duplicate_campaign).HasValue(), "duplicate target was planned");

  vucm::VehicleUpdateManager abort_manager{Policy()};
  Require(abort_manager.RegisterEcu(Ecu("adaptive-qemu")).HasValue(), "abort ECU register failed");
  auto abort_campaign = Campaign();
  abort_campaign.packages.resize(1U);
  Require(abort_manager.PlanCampaign(abort_campaign).HasValue(), "abort campaign plan failed");
  auto aborted = abort_manager.AbortAndRollback("campaign-ultrasonic-1", "operator abort");
  Require(aborted.HasValue(), "campaign abort failed");
  Require(aborted.Value().state == vucm::CampaignState::kRolledBack, "abort state changed");

  Require(vucm::ToString(vucm::CampaignState::kCommitted) == std::string_view("Committed"),
          "vehicle campaign state text changed");
  Require(vucm::ToString(vucm::EcuAvailability::kRecoveryRequired) ==
            std::string_view("RecoveryRequired"),
          "ECU availability text changed");

  return 0;
}
