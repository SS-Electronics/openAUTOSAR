// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::runtime::registry {

enum class RegistryKind {
  kMachine,
  kProcess,
  kService,
  kFunctionGroup,
  kSoftwareCluster,
  kDiagnostic,
  kPolicy,
};

enum class RegistryState {
  kDeclared,
  kActive,
  kStale,
  kRetired,
};

struct RegistryRecord final {
  std::string record_id;
  RegistryKind kind{RegistryKind::kProcess};
  RegistryState state{RegistryState::kDeclared};
  std::string version;
  std::string owner;
  std::string endpoint;
  std::string source_model;
  std::uint64_t last_update_ms{0U};
  std::uint64_t lease_ttl_ms{0U};
};

struct RegistryPolicy final {
  std::size_t max_records{256U};
  bool require_source_model{true};
  bool reject_duplicate_active_records{true};
  std::uint64_t default_lease_ttl_ms{1'000U};
};

struct RegistryQuery final {
  std::optional<RegistryKind> kind;
  std::optional<RegistryState> state;
  std::string owner_prefix;
  std::string record_prefix;
};

struct RegistrySnapshot final {
  std::vector<RegistryRecord> records;
  std::uint32_t active_records{0U};
  std::uint32_t stale_records{0U};
  std::uint32_t retired_records{0U};
  std::uint32_t rejected_updates{0U};
};

class RegistryManager final {
public:
  explicit RegistryManager(RegistryPolicy policy = {});

  [[nodiscard]] core::Result<RegistryRecord> Register(RegistryRecord record);
  [[nodiscard]] core::Result<RegistryRecord> UpdateState(
    std::string_view record_id,
    RegistryKind kind,
    RegistryState state,
    std::uint64_t timestamp_ms);
  [[nodiscard]] core::Result<RegistryRecord> Retire(
    std::string_view record_id,
    RegistryKind kind,
    std::uint64_t timestamp_ms);
  [[nodiscard]] std::optional<RegistryRecord> Lookup(
    std::string_view record_id,
    RegistryKind kind) const;
  [[nodiscard]] std::vector<RegistryRecord> Query(RegistryQuery query = {}) const;
  [[nodiscard]] RegistrySnapshot Snapshot() const;

  void MarkStale(std::uint64_t now_ms);

private:
  [[nodiscard]] core::Result<bool> ValidateRecord(const RegistryRecord& record) const;
  [[nodiscard]] std::optional<std::size_t> FindIndex(
    std::string_view record_id,
    RegistryKind kind) const;
  [[nodiscard]] bool HasConflictingActiveRecord(const RegistryRecord& record) const;

  RegistryPolicy policy_{};
  std::vector<RegistryRecord> records_;
  std::uint32_t rejected_updates_{0U};
};

[[nodiscard]] std::string_view ToString(RegistryKind kind) noexcept;
[[nodiscard]] std::string_view ToString(RegistryState state) noexcept;

}  // namespace openautosar::runtime::registry
