// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openautosar::runtime::persistency {

enum class BackendKind {
  kInMemory,
  kFilesystem,
};

enum class StoreHealth {
  kHealthy,
  kRecovered,
};

enum class RemoteTransport {
  kMutualTls,
  kLocalIpcGateway,
  kTestLoopback,
};

enum class RemoteGuarantee {
  kConfidentiality,
  kIntegrity,
  kAvailability,
  kRollbackProtection,
  kDataResidency,
  kLatency,
  kMutualAuthentication,
};

enum class RemoteOperation {
  kMirrorPut,
  kRestoreRequest,
};

struct AccessPolicy final {
  std::string key_prefix;
  bool read_only{false};
  bool allow_remove{true};
};

struct StoreConfig final {
  std::string application_id;
  std::uint32_t schema_version{1U};
  std::size_t quota_bytes{4096U};
  AccessPolicy access{};
  std::filesystem::path root;
};

struct EntryMetadata final {
  std::string key;
  std::uint32_t schema_version{1U};
  std::size_t size{0U};
  std::uint32_t checksum{0U};
};

struct StoreSnapshot final {
  BackendKind backend{BackendKind::kInMemory};
  StoreHealth health{StoreHealth::kHealthy};
  std::uint32_t schema_version{1U};
  std::size_t used_bytes{0U};
  std::size_t quota_bytes{0U};
  std::uint32_t recovered_entries{0U};
  std::vector<EntryMetadata> entries;
};

struct RemotePersistencyPolicy final {
  std::string application_id;
  std::uint32_t schema_version{1U};
  std::string key_prefix;
  std::size_t quota_bytes{4096U};
  bool allow_remote_writes{false};
  bool require_confidentiality{true};
  bool require_integrity{true};
  bool require_mutual_authentication{true};
  bool require_rollback_protection{true};
  std::uint8_t min_availability_class{1U};
  std::string required_data_residency_region;
  std::uint32_t max_latency_ms{100U};
  std::string audit_label;
};

struct RemotePersistencyTarget final {
  std::string endpoint;
  std::string data_residency_region;
  RemoteTransport transport{RemoteTransport::kMutualTls};
  bool confidentiality_enabled{true};
  bool integrity_enabled{true};
  bool mutual_authentication{true};
  bool rollback_protection_enabled{true};
  std::uint8_t availability_class{1U};
  std::uint32_t advertised_latency_ms{0U};
};

struct RemoteGuaranteeGap final {
  RemoteGuarantee guarantee{RemoteGuarantee::kConfidentiality};
  std::string required;
  std::string advertised;
};

struct RemoteValidationReport final {
  bool accepted{false};
  std::string endpoint;
  std::vector<RemoteGuaranteeGap> weakened_guarantees;
};

struct RemoteOperationReceipt final {
  bool accepted{false};
  RemoteOperation operation{RemoteOperation::kMirrorPut};
  std::string endpoint;
  std::string key;
  std::uint32_t schema_version{1U};
  std::size_t value_size{0U};
  std::uint32_t checksum{0U};
  std::uint64_t sequence{0U};
  std::uint64_t monotonic_ms{0U};
  std::string audit_label;
};

struct RemotePersistencySnapshot final {
  RemoteValidationReport validation;
  std::uint64_t accepted_operations{0U};
  std::uint64_t blocked_operations{0U};
  std::vector<RemoteOperationReceipt> receipts;
};

class KeyValueStore final {
public:
  struct Entry final {
    std::uint32_t schema_version{1U};
    std::vector<std::uint8_t> value;
    std::uint32_t checksum{0U};
  };

  [[nodiscard]] static core::Result<KeyValueStore> OpenInMemory(StoreConfig config);
  [[nodiscard]] static core::Result<KeyValueStore> OpenFilesystem(StoreConfig config);

  [[nodiscard]] core::Result<bool> Put(std::string_view key, std::span<const std::uint8_t> value);
  [[nodiscard]] core::Result<std::vector<std::uint8_t>> Get(std::string_view key) const;
  [[nodiscard]] core::Result<bool> Remove(std::string_view key);

  [[nodiscard]] core::Result<bool> MigrateSchema(std::uint32_t next_schema_version);
  [[nodiscard]] core::Result<bool> BackupTo(const std::filesystem::path& backup_directory) const;
  [[nodiscard]] core::Result<bool> RestoreFrom(const std::filesystem::path& backup_directory);
  [[nodiscard]] core::Result<bool> ImportFile(
    std::string_view key,
    const std::filesystem::path& source);
  [[nodiscard]] core::Result<bool> ExportFile(
    std::string_view key,
    const std::filesystem::path& destination) const;

  [[nodiscard]] core::Result<bool> Flush() const;
  [[nodiscard]] core::Result<bool> Shutdown();
  [[nodiscard]] StoreSnapshot Snapshot() const;

private:
  KeyValueStore(StoreConfig config, BackendKind backend) noexcept;

  [[nodiscard]] core::Result<bool> ValidateKey(std::string_view key) const;
  [[nodiscard]] core::Result<bool> EnsureWritable() const;
  [[nodiscard]] core::Result<bool> EnsureQuota(
    std::string_view key,
    std::size_t replacement_size) const;
  [[nodiscard]] core::Result<bool> WriteEntryFile(std::string_view key, const Entry& entry) const;
  [[nodiscard]] core::Result<bool> RemoveEntryFile(std::string_view key) const;
  [[nodiscard]] core::Result<bool> LoadFilesystem();
  [[nodiscard]] std::filesystem::path StoreDirectory() const;

  StoreConfig config_{};
  BackendKind backend_{BackendKind::kInMemory};
  StoreHealth health_{StoreHealth::kHealthy};
  std::uint32_t recovered_entries_{0U};
  std::unordered_map<std::string, Entry> entries_;
};

class RemotePersistencyClient final {
public:
  [[nodiscard]] static core::Result<RemotePersistencyClient> Create(
    RemotePersistencyPolicy policy,
    RemotePersistencyTarget target);

  [[nodiscard]] core::Result<RemoteValidationReport> ValidateConnection() const;
  [[nodiscard]] core::Result<RemoteOperationReceipt> MirrorPut(
    std::string_view key,
    std::span<const std::uint8_t> value,
    std::uint32_t schema_version,
    std::uint64_t monotonic_ms);
  [[nodiscard]] core::Result<RemoteOperationReceipt> RequestRestore(
    std::string_view key,
    std::uint32_t expected_schema_version,
    std::uint64_t monotonic_ms);
  [[nodiscard]] RemotePersistencySnapshot Snapshot() const;

private:
  RemotePersistencyClient(
    RemotePersistencyPolicy policy,
    RemotePersistencyTarget target,
    RemoteValidationReport validation) noexcept;

  [[nodiscard]] core::Result<bool> ValidateOperation(
    RemoteOperation operation,
    std::string_view key,
    std::uint32_t schema_version,
    std::size_t value_size) const;
  [[nodiscard]] RemoteOperationReceipt MakeReceipt(
    RemoteOperation operation,
    std::string_view key,
    std::uint32_t schema_version,
    std::size_t value_size,
    std::uint32_t checksum,
    std::uint64_t monotonic_ms) const;
  [[nodiscard]] core::Result<RemoteOperationReceipt> BlockReceipt(
    RemoteOperationReceipt receipt,
    core::ErrorCode error);

  RemotePersistencyPolicy policy_{};
  RemotePersistencyTarget target_{};
  RemoteValidationReport validation_{};
  std::uint64_t sequence_{0U};
  std::uint64_t accepted_operations_{0U};
  std::uint64_t blocked_operations_{0U};
  std::vector<RemoteOperationReceipt> receipts_;
};

[[nodiscard]] std::string_view ToString(BackendKind backend) noexcept;
[[nodiscard]] std::string_view ToString(StoreHealth health) noexcept;
[[nodiscard]] std::string_view ToString(RemoteTransport transport) noexcept;
[[nodiscard]] std::string_view ToString(RemoteGuarantee guarantee) noexcept;
[[nodiscard]] std::string_view ToString(RemoteOperation operation) noexcept;

}  // namespace openautosar::runtime::persistency
