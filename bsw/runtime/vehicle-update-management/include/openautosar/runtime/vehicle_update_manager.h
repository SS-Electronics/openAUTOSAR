// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/runtime/update_manager.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::runtime::vucm {

enum class EcuAvailability {
  kOnline,
  kOffline,
  kUpdating,
  kRecoveryRequired,
};

enum class CampaignState {
  kIdle,
  kPlanned,
  kAwaitingActivation,
  kAwaitingHealthConfirmation,
  kCommitted,
  kRolledBack,
  kRejected,
  kFailed,
};

enum class StepState {
  kPending,
  kReady,
  kActivated,
  kCommitted,
  kRolledBack,
  kBlocked,
  kFailed,
};

struct EcuInventory final {
  std::string ecu_id;
  std::string machine_id;
  EcuAvailability availability{EcuAvailability::kOnline};
  ucm::MachineState machine_state{ucm::MachineState::kStartup};
  bool rollback_supported{true};
  std::size_t inactive_slot_free_bytes{0U};
  std::map<std::string, std::string> installed_versions;
};

struct CampaignPackage final {
  std::string ecu_id;
  ucm::SoftwareClusterPackage package;
  ucm::MachineState required_activation_state{ucm::MachineState::kUpdateAllowed};
};

struct VehicleUpdatePolicy final {
  std::size_t max_steps{16U};
  bool require_all_targets_online{true};
  bool require_rollback_capable{true};
  bool require_sbom{true};
  bool require_provenance{true};
  bool require_health_confirmation{true};
  std::vector<std::string> trusted_signers;
};

struct VehicleCampaignRequest final {
  std::string campaign_id;
  std::vector<CampaignPackage> packages;
};

struct CampaignStep final {
  std::uint32_t sequence{0U};
  std::string ecu_id;
  ucm::SoftwareClusterPackage package;
  ucm::MachineState required_activation_state{ucm::MachineState::kUpdateAllowed};
  StepState state{StepState::kPending};
  std::string reason;
};

struct VehicleCampaignAudit final {
  CampaignState state{CampaignState::kIdle};
  std::string ecu_id;
  std::string cluster_name;
  std::string message;
};

struct VehicleCampaignPlan final {
  std::string campaign_id;
  CampaignState state{CampaignState::kIdle};
  std::vector<CampaignStep> steps;
  std::vector<VehicleCampaignAudit> audit;
};

class VehicleUpdateManager final {
public:
  explicit VehicleUpdateManager(VehicleUpdatePolicy policy = {});

  [[nodiscard]] core::Result<bool> RegisterEcu(EcuInventory inventory);
  [[nodiscard]] core::Result<VehicleCampaignPlan> PlanCampaign(
    VehicleCampaignRequest request);
  [[nodiscard]] core::Result<VehicleCampaignPlan> ActivateCampaign(
    std::string_view campaign_id,
    ucm::MachineState vehicle_state);
  [[nodiscard]] core::Result<VehicleCampaignPlan> ConfirmEcuHealth(
    std::string_view campaign_id,
    std::string_view ecu_id,
    ucm::HealthConfirmation confirmation);
  [[nodiscard]] core::Result<VehicleCampaignPlan> AbortAndRollback(
    std::string_view campaign_id,
    std::string reason);

  [[nodiscard]] std::optional<VehicleCampaignPlan> ActiveCampaign() const {
    return active_campaign_;
  }
  [[nodiscard]] std::optional<EcuInventory> InventoryFor(
    std::string_view ecu_id) const;

private:
  [[nodiscard]] core::Result<bool> ValidateRequest(
    const VehicleCampaignRequest& request) const;
  [[nodiscard]] core::Result<bool> ValidatePackage(
    const CampaignPackage& item) const;
  [[nodiscard]] bool IsTrustedSigner(std::string_view signer_id) const noexcept;
  [[nodiscard]] bool IsVersionUpgrade(const CampaignPackage& item) const noexcept;
  [[nodiscard]] bool AreDependenciesInstalled(const CampaignPackage& item) const noexcept;
  [[nodiscard]] bool HealthConfirmed(const ucm::HealthConfirmation& confirmation) const noexcept;

  void Audit(
    CampaignState state,
    std::string message,
    std::string ecu_id = {},
    std::string cluster_name = {});

  VehicleUpdatePolicy policy_{};
  std::map<std::string, EcuInventory> inventory_;
  std::optional<VehicleCampaignPlan> active_campaign_;
};

[[nodiscard]] std::string_view ToString(EcuAvailability availability) noexcept;
[[nodiscard]] std::string_view ToString(CampaignState state) noexcept;
[[nodiscard]] std::string_view ToString(StepState state) noexcept;

}  // namespace openautosar::runtime::vucm
