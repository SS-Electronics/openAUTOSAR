// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/runtime/platform_health_manager.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::runtime::ucm {

enum class MachineState {
  kStartup,
  kDrivingReady,
  kUpdateAllowed,
  kDegraded,
  kShutdown,
};

enum class TransactionState {
  kIdle,
  kTransferred,
  kVerified,
  kStaged,
  kPendingActivation,
  kPendingHealthConfirmation,
  kCommitted,
  kRolledBack,
  kRejected,
  kFailed,
};

enum class BackendOperation {
  kNone,
  kInstallInactive,
  kActivatePending,
  kCommit,
  kRollback,
};

struct SoftwareClusterPackage final {
  std::string cluster_name;
  std::string version;
  std::string signer_id;
  bool signature_verified{false};
  bool compatible_platform{false};
  std::size_t required_free_bytes{0U};
  std::vector<std::string> dependencies;
  bool manifests_present{false};
  bool sbom_present{false};
  bool provenance_present{false};

  friend bool operator==(const SoftwareClusterPackage&, const SoftwareClusterPackage&) = default;
};

struct UpdatePolicy final {
  MachineState activation_state{MachineState::kUpdateAllowed};
  std::uint32_t max_boot_attempts{2U};
  bool require_sbom{true};
  bool require_provenance{true};
  std::vector<std::string> trusted_signers;
  std::map<std::string, std::string> installed_versions;
};

struct SlotStatus final {
  std::string active_slot{"A"};
  std::string inactive_slot{"B"};
  std::optional<std::string> pending_slot;
  std::optional<SoftwareClusterPackage> installed_in_inactive_slot;
  std::optional<SoftwareClusterPackage> active_package;
  std::uint32_t boot_attempts_remaining{0U};
  std::size_t inactive_slot_free_bytes{0U};

  friend bool operator==(const SlotStatus&, const SlotStatus&) = default;
};

struct HealthConfirmation final {
  bool execution_started{false};
  phm::HealthState platform_health{phm::HealthState::kFailed};
  bool diagnostics_available{false};
  bool dashboard_available{false};
};

struct AuditEvent final {
  TransactionState state{TransactionState::kIdle};
  std::string message;
  std::optional<BackendOperation> backend_operation;
};

class IUpdateBackend {
public:
  IUpdateBackend() = default;
  IUpdateBackend(const IUpdateBackend&) = delete;
  IUpdateBackend& operator=(const IUpdateBackend&) = delete;
  virtual ~IUpdateBackend() = default;

  [[nodiscard]] virtual core::Result<SlotStatus> Status() const = 0;
  [[nodiscard]] virtual core::Result<bool> InstallInactive(
    const SoftwareClusterPackage& package) = 0;
  [[nodiscard]] virtual core::Result<bool> ActivatePending(std::uint32_t boot_attempts) = 0;
  [[nodiscard]] virtual core::Result<bool> Commit() = 0;
  [[nodiscard]] virtual core::Result<bool> Rollback() = 0;
};

class RaucBackendSimulator final : public IUpdateBackend {
public:
  explicit RaucBackendSimulator(
    std::size_t inactive_slot_free_bytes,
    SoftwareClusterPackage active_package);

  [[nodiscard]] core::Result<SlotStatus> Status() const override;
  [[nodiscard]] core::Result<bool> InstallInactive(
    const SoftwareClusterPackage& package) override;
  [[nodiscard]] core::Result<bool> ActivatePending(std::uint32_t boot_attempts) override;
  [[nodiscard]] core::Result<bool> Commit() override;
  [[nodiscard]] core::Result<bool> Rollback() override;

  void FailNext(BackendOperation operation) noexcept;

private:
  [[nodiscard]] core::Result<bool> ConsumeFailure(BackendOperation operation);
  void SwapSlots();

  SlotStatus status_{};
  std::optional<SoftwareClusterPackage> previous_active_package_;
  std::optional<BackendOperation> fail_next_;
};

class UpdateManager final {
public:
  UpdateManager(IUpdateBackend& backend, UpdatePolicy policy);

  [[nodiscard]] core::Result<TransactionState> TransferPackage(SoftwareClusterPackage package);
  [[nodiscard]] core::Result<TransactionState> VerifyPackage();
  [[nodiscard]] core::Result<TransactionState> StagePackage();
  [[nodiscard]] core::Result<TransactionState> Activate(MachineState machine_state);
  [[nodiscard]] core::Result<TransactionState> ConfirmHealth(HealthConfirmation confirmation);
  [[nodiscard]] core::Result<TransactionState> AbortAndRollback(std::string reason);

  [[nodiscard]] TransactionState State() const noexcept { return state_; }
  [[nodiscard]] const std::vector<AuditEvent>& AuditTrail() const noexcept { return audit_; }
  [[nodiscard]] const std::optional<SoftwareClusterPackage>& PendingPackage() const noexcept {
    return pending_package_;
  }

private:
  [[nodiscard]] core::Result<bool> ValidatePackage(const SoftwareClusterPackage& package) const;
  [[nodiscard]] bool IsTrustedSigner(std::string_view signer_id) const noexcept;
  [[nodiscard]] bool AreDependenciesInstalled(const SoftwareClusterPackage& package) const noexcept;
  [[nodiscard]] bool IsVersionUpgrade(const SoftwareClusterPackage& package) const noexcept;
  [[nodiscard]] static bool HealthConfirmed(const HealthConfirmation& confirmation) noexcept;

  void Audit(
    TransactionState state,
    std::string message,
    std::optional<BackendOperation> operation = std::nullopt);

  IUpdateBackend& backend_;
  UpdatePolicy policy_;
  TransactionState state_{TransactionState::kIdle};
  std::optional<SoftwareClusterPackage> pending_package_;
  std::vector<AuditEvent> audit_;
};

[[nodiscard]] std::string_view ToString(MachineState state) noexcept;
[[nodiscard]] std::string_view ToString(TransactionState state) noexcept;
[[nodiscard]] std::string_view ToString(BackendOperation operation) noexcept;

}  // namespace openautosar::runtime::ucm
