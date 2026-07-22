// SPDX-License-Identifier: MIT

#include "openautosar/runtime/registry_manager.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace openautosar::runtime::registry {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"registry", message};
}

[[nodiscard]] bool IsSafeToken(std::string_view value) noexcept {
  if (value.empty()) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](unsigned char value_char) {
    return std::isalnum(value_char) != 0 || value_char == '_' || value_char == '-' ||
           value_char == '.' || value_char == ':' || value_char == '/';
  });
}

[[nodiscard]] bool PrefixMatches(std::string_view value, std::string_view prefix) noexcept {
  return prefix.empty() || value.rfind(prefix, 0U) == 0U;
}

[[nodiscard]] bool MatchesQuery(
  const RegistryRecord& record,
  const RegistryQuery& query) noexcept {
  if (query.kind.has_value() && record.kind != query.kind.value()) {
    return false;
  }

  if (query.state.has_value() && record.state != query.state.value()) {
    return false;
  }

  return PrefixMatches(record.owner, query.owner_prefix) &&
         PrefixMatches(record.record_id, query.record_prefix);
}

[[nodiscard]] bool IsLeased(const RegistryRecord& record) noexcept {
  return record.lease_ttl_ms > 0U;
}

}  // namespace

RegistryManager::RegistryManager(RegistryPolicy policy) : policy_(policy) {}

core::Result<RegistryRecord> RegistryManager::Register(RegistryRecord record) {
  auto valid = ValidateRecord(record);
  if (!valid) {
    ++rejected_updates_;
    return core::Result<RegistryRecord>::FromError(valid.Error());
  }

  if (record.lease_ttl_ms == 0U) {
    record.lease_ttl_ms = policy_.default_lease_ttl_ms;
  }

  if (policy_.reject_duplicate_active_records && HasConflictingActiveRecord(record)) {
    ++rejected_updates_;
    return core::Result<RegistryRecord>::FromError(
      MakeError("registry record conflicts with active entry"));
  }

  const auto index = FindIndex(record.record_id, record.kind);
  if (index.has_value()) {
    records_[index.value()] = record;
  } else {
    if (records_.size() >= policy_.max_records) {
      ++rejected_updates_;
      return core::Result<RegistryRecord>::FromError(
        MakeError("registry record budget exceeded"));
    }
    records_.push_back(record);
  }

  return core::Result<RegistryRecord>::FromValue(std::move(record));
}

core::Result<RegistryRecord> RegistryManager::UpdateState(
  std::string_view record_id,
  RegistryKind kind,
  RegistryState state,
  std::uint64_t timestamp_ms) {
  const auto index = FindIndex(record_id, kind);
  if (!index.has_value()) {
    ++rejected_updates_;
    return core::Result<RegistryRecord>::FromError(MakeError("registry record was not found"));
  }

  auto& record = records_[index.value()];
  if (record.state == RegistryState::kRetired && state != RegistryState::kDeclared) {
    ++rejected_updates_;
    return core::Result<RegistryRecord>::FromError(
      MakeError("retired registry record cannot be reactivated"));
  }

  record.state = state;
  record.last_update_ms = timestamp_ms;
  return core::Result<RegistryRecord>::FromValue(record);
}

core::Result<RegistryRecord> RegistryManager::Retire(
  std::string_view record_id,
  RegistryKind kind,
  std::uint64_t timestamp_ms) {
  return UpdateState(record_id, kind, RegistryState::kRetired, timestamp_ms);
}

std::optional<RegistryRecord> RegistryManager::Lookup(
  std::string_view record_id,
  RegistryKind kind) const {
  const auto index = FindIndex(record_id, kind);
  if (!index.has_value()) {
    return std::nullopt;
  }

  return records_[index.value()];
}

std::vector<RegistryRecord> RegistryManager::Query(RegistryQuery query) const {
  std::vector<RegistryRecord> results;
  for (const auto& record : records_) {
    if (MatchesQuery(record, query)) {
      results.push_back(record);
    }
  }

  std::sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
    if (left.kind == right.kind) {
      return left.record_id < right.record_id;
    }
    return ToString(left.kind) < ToString(right.kind);
  });
  return results;
}

RegistrySnapshot RegistryManager::Snapshot() const {
  RegistrySnapshot snapshot{
    .records = Query({}),
    .rejected_updates = rejected_updates_,
  };
  for (const auto& record : snapshot.records) {
    if (record.state == RegistryState::kActive) {
      ++snapshot.active_records;
    } else if (record.state == RegistryState::kStale) {
      ++snapshot.stale_records;
    } else if (record.state == RegistryState::kRetired) {
      ++snapshot.retired_records;
    }
  }

  return snapshot;
}

void RegistryManager::MarkStale(std::uint64_t now_ms) {
  for (auto& record : records_) {
    if (record.state != RegistryState::kActive || !IsLeased(record)) {
      continue;
    }

    const auto age = now_ms >= record.last_update_ms ? now_ms - record.last_update_ms : 0U;
    if (age > record.lease_ttl_ms) {
      record.state = RegistryState::kStale;
      record.last_update_ms = now_ms;
    }
  }
}

core::Result<bool> RegistryManager::ValidateRecord(const RegistryRecord& record) const {
  if (policy_.max_records == 0U) {
    return core::Result<bool>::FromError(MakeError("registry record budget is zero"));
  }

  if (!IsSafeToken(record.record_id) || !IsSafeToken(record.version) ||
      !IsSafeToken(record.owner)) {
    return core::Result<bool>::FromError(MakeError("registry record identity is invalid"));
  }

  if (!record.endpoint.empty() && !IsSafeToken(record.endpoint)) {
    return core::Result<bool>::FromError(MakeError("registry record endpoint is invalid"));
  }

  if (policy_.require_source_model && record.source_model.empty()) {
    return core::Result<bool>::FromError(MakeError("registry source model is required"));
  }

  if (!record.source_model.empty() && !IsSafeToken(record.source_model)) {
    return core::Result<bool>::FromError(MakeError("registry source model token is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

std::optional<std::size_t> RegistryManager::FindIndex(
  std::string_view record_id,
  RegistryKind kind) const {
  for (std::size_t index = 0U; index < records_.size(); ++index) {
    if (records_[index].record_id == record_id && records_[index].kind == kind) {
      return index;
    }
  }

  return std::nullopt;
}

bool RegistryManager::HasConflictingActiveRecord(const RegistryRecord& record) const {
  const auto index = FindIndex(record.record_id, record.kind);
  if (!index.has_value()) {
    return false;
  }

  const auto& existing = records_[index.value()];
  return existing.state == RegistryState::kActive &&
         record.state == RegistryState::kActive &&
         existing.owner != record.owner;
}

std::string_view ToString(RegistryKind kind) noexcept {
  switch (kind) {
    case RegistryKind::kMachine:
      return "Machine";
    case RegistryKind::kProcess:
      return "Process";
    case RegistryKind::kService:
      return "Service";
    case RegistryKind::kFunctionGroup:
      return "FunctionGroup";
    case RegistryKind::kSoftwareCluster:
      return "SoftwareCluster";
    case RegistryKind::kDiagnostic:
      return "Diagnostic";
    case RegistryKind::kPolicy:
      return "Policy";
  }

  return "Unknown";
}

std::string_view ToString(RegistryState state) noexcept {
  switch (state) {
    case RegistryState::kDeclared:
      return "Declared";
    case RegistryState::kActive:
      return "Active";
    case RegistryState::kStale:
      return "Stale";
    case RegistryState::kRetired:
      return "Retired";
  }

  return "Unknown";
}

}  // namespace openautosar::runtime::registry
