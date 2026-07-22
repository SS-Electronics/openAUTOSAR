// SPDX-License-Identifier: MIT

#include "openautosar/runtime/persistency.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <utility>

namespace openautosar::runtime::persistency {
namespace {

inline constexpr std::string_view kFileMagic{"OAKV1"};
inline constexpr std::array<char, 16U> kHexDigits{
  '0',
  '1',
  '2',
  '3',
  '4',
  '5',
  '6',
  '7',
  '8',
  '9',
  'a',
  'b',
  'c',
  'd',
  'e',
  'f',
};

struct DecodedEntry final {
  std::string key;
  KeyValueStore::Entry entry;
};

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"persistency", message};
}

[[nodiscard]] bool IsSafeApplicationId(std::string_view application_id) noexcept {
  if (application_id.empty() || application_id.size() > 64U) {
    return false;
  }

  return std::all_of(application_id.begin(), application_id.end(), [](char value) {
    const auto byte = static_cast<unsigned char>(value);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || value == '-' || value == '_' || value == '.';
  });
}

[[nodiscard]] bool IsSafeKeyCharacter(char value) noexcept {
  const auto byte = static_cast<unsigned char>(value);
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') || value == '-' || value == '_' || value == '.' ||
         value == '/';
}

[[nodiscard]] core::Result<bool> ValidateKeyForPolicy(
  std::string_view key,
  std::string_view key_prefix) {
  if (key.empty() || key.size() > 128U) {
    return core::Result<bool>::FromError(MakeError("persistency key is invalid"));
  }

  if (key.front() == '/' || key.find("..") != std::string_view::npos) {
    return core::Result<bool>::FromError(MakeError("persistency key path is unsafe"));
  }

  if (!std::all_of(key.begin(), key.end(), IsSafeKeyCharacter)) {
    return core::Result<bool>::FromError(MakeError("persistency key contains invalid characters"));
  }

  if (!key_prefix.empty() && key.rfind(key_prefix, 0U) != 0U) {
    return core::Result<bool>::FromError(MakeError("persistency key is denied by policy"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateConfig(
  const StoreConfig& config,
  BackendKind backend) {
  if (!IsSafeApplicationId(config.application_id)) {
    return core::Result<bool>::FromError(MakeError("application id is invalid"));
  }

  if (config.schema_version == 0U) {
    return core::Result<bool>::FromError(MakeError("schema version is zero"));
  }

  if (config.quota_bytes == 0U) {
    return core::Result<bool>::FromError(MakeError("persistency quota is zero"));
  }

  if (backend == BackendKind::kFilesystem && config.root.empty()) {
    return core::Result<bool>::FromError(MakeError("filesystem persistency root is empty"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateRemotePolicy(
  const RemotePersistencyPolicy& policy) {
  if (!IsSafeApplicationId(policy.application_id)) {
    return core::Result<bool>::FromError(MakeError("remote application id is invalid"));
  }

  if (policy.schema_version == 0U) {
    return core::Result<bool>::FromError(MakeError("remote schema version is zero"));
  }

  if (policy.quota_bytes == 0U) {
    return core::Result<bool>::FromError(MakeError("remote persistency quota is zero"));
  }

  if (policy.max_latency_ms == 0U) {
    return core::Result<bool>::FromError(MakeError("remote latency budget is zero"));
  }

  if (policy.required_data_residency_region.empty()) {
    return core::Result<bool>::FromError(MakeError("remote data residency region is empty"));
  }

  if (!policy.key_prefix.empty()) {
    auto prefix_validation = ValidateKeyForPolicy(policy.key_prefix, {});
    if (!prefix_validation) {
      return core::Result<bool>::FromError(
        MakeError("remote persistency key prefix is invalid"));
    }
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateRemoteTarget(
  const RemotePersistencyTarget& target) {
  if (target.endpoint.empty() || target.endpoint.size() > 160U) {
    return core::Result<bool>::FromError(MakeError("remote endpoint is invalid"));
  }

  if (target.data_residency_region.empty()) {
    return core::Result<bool>::FromError(MakeError("remote target region is empty"));
  }

  if (target.availability_class == 0U) {
    return core::Result<bool>::FromError(MakeError("remote availability class is zero"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] std::string AvailabilityText(std::uint8_t availability_class) {
  return "class-" + std::to_string(static_cast<unsigned int>(availability_class));
}

[[nodiscard]] std::string LatencyText(std::uint32_t latency_ms) {
  return std::to_string(latency_ms) + "ms";
}

void AddGap(
  RemoteValidationReport& report,
  RemoteGuarantee guarantee,
  std::string required,
  std::string advertised) {
  report.weakened_guarantees.push_back({
    .guarantee = guarantee,
    .required = std::move(required),
    .advertised = std::move(advertised),
  });
}

[[nodiscard]] RemoteValidationReport BuildRemoteValidationReport(
  const RemotePersistencyPolicy& policy,
  const RemotePersistencyTarget& target) {
  RemoteValidationReport report{
    .accepted = false,
    .endpoint = target.endpoint,
    .weakened_guarantees = {},
  };

  if (policy.require_confidentiality && !target.confidentiality_enabled) {
    AddGap(report, RemoteGuarantee::kConfidentiality, "true", "false");
  }

  if (policy.require_integrity && !target.integrity_enabled) {
    AddGap(report, RemoteGuarantee::kIntegrity, "true", "false");
  }

  if (policy.require_mutual_authentication && !target.mutual_authentication) {
    AddGap(report, RemoteGuarantee::kMutualAuthentication, "true", "false");
  }

  if (policy.require_rollback_protection && !target.rollback_protection_enabled) {
    AddGap(report, RemoteGuarantee::kRollbackProtection, "true", "false");
  }

  if (target.availability_class < policy.min_availability_class) {
    AddGap(
      report,
      RemoteGuarantee::kAvailability,
      AvailabilityText(policy.min_availability_class),
      AvailabilityText(target.availability_class));
  }

  if (target.data_residency_region != policy.required_data_residency_region) {
    AddGap(
      report,
      RemoteGuarantee::kDataResidency,
      policy.required_data_residency_region,
      target.data_residency_region);
  }

  if (target.advertised_latency_ms > policy.max_latency_ms) {
    AddGap(
      report,
      RemoteGuarantee::kLatency,
      LatencyText(policy.max_latency_ms),
      LatencyText(target.advertised_latency_ms));
  }

  report.accepted = report.weakened_guarantees.empty();
  return report;
}

[[nodiscard]] std::uint32_t Checksum(std::span<const std::uint8_t> value) noexcept {
  std::uint32_t hash{2166136261U};
  for (const auto byte : value) {
    hash ^= byte;
    hash *= 16777619U;
  }

  return hash;
}

[[nodiscard]] std::string EncodedFileName(std::string_view key) {
  std::string encoded;
  encoded.reserve((key.size() * 2U) + 5U);
  for (const auto value : key) {
    const auto byte = static_cast<unsigned char>(value);
    encoded.push_back(kHexDigits[(byte >> 4U) & 0x0FU]);
    encoded.push_back(kHexDigits[byte & 0x0FU]);
  }

  encoded += ".oakv";
  return encoded;
}

[[nodiscard]] bool HasOakvExtension(const std::filesystem::path& path) {
  return path.extension() == ".oakv";
}

[[nodiscard]] core::Result<std::uint32_t> ParseU32(std::string_view text) {
  std::uint32_t value{0U};
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return core::Result<std::uint32_t>::FromError(MakeError("integer field is invalid"));
  }

  return core::Result<std::uint32_t>::FromValue(value);
}

[[nodiscard]] core::Result<std::size_t> ParseSize(std::string_view text) {
  std::size_t value{0U};
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return core::Result<std::size_t>::FromError(MakeError("size field is invalid"));
  }

  return core::Result<std::size_t>::FromValue(value);
}

[[nodiscard]] core::Result<std::string> FieldValue(
  const std::string& line,
  std::string_view prefix) {
  if (line.rfind(prefix, 0U) != 0U) {
    return core::Result<std::string>::FromError(MakeError("persistency field is missing"));
  }

  return core::Result<std::string>::FromValue(line.substr(prefix.size()));
}

[[nodiscard]] core::Result<DecodedEntry> ReadEntryFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return core::Result<DecodedEntry>::FromError(MakeError("persistency file did not open"));
  }

  std::string magic;
  std::string schema_line;
  std::string key_line;
  std::string size_line;
  std::string checksum_line;
  std::string separator;
  if (!std::getline(input, magic) || !std::getline(input, schema_line) ||
      !std::getline(input, key_line) || !std::getline(input, size_line) ||
      !std::getline(input, checksum_line) || !std::getline(input, separator)) {
    return core::Result<DecodedEntry>::FromError(MakeError("persistency file header is truncated"));
  }

  if (magic != kFileMagic || !separator.empty()) {
    return core::Result<DecodedEntry>::FromError(MakeError("persistency file header is invalid"));
  }

  auto schema_text = FieldValue(schema_line, "schema=");
  auto key = FieldValue(key_line, "key=");
  auto size_text = FieldValue(size_line, "size=");
  auto checksum_text = FieldValue(checksum_line, "checksum=");
  if (!schema_text || !key || !size_text || !checksum_text) {
    return core::Result<DecodedEntry>::FromError(MakeError("persistency file field is invalid"));
  }

  auto schema = ParseU32(schema_text.Value());
  auto size = ParseSize(size_text.Value());
  auto checksum = ParseU32(checksum_text.Value());
  if (!schema || !size || !checksum) {
    return core::Result<DecodedEntry>::FromError(MakeError("persistency file metadata is invalid"));
  }

  std::vector<std::uint8_t> value{
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>()};
  if (value.size() != size.Value()) {
    return core::Result<DecodedEntry>::FromError(MakeError("persistency file size mismatch"));
  }

  if (Checksum(value) != checksum.Value()) {
    return core::Result<DecodedEntry>::FromError(MakeError("persistency checksum mismatch"));
  }

  DecodedEntry decoded{
    .key = std::move(key.Value()),
    .entry = {
      .schema_version = schema.Value(),
      .value = std::move(value),
      .checksum = checksum.Value(),
    },
  };
  return core::Result<DecodedEntry>::FromValue(std::move(decoded));
}

[[nodiscard]] core::Result<bool> WriteEntryFileToDirectory(
  const std::filesystem::path& directory,
  std::string_view key,
  const KeyValueStore::Entry& entry) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    return core::Result<bool>::FromError(MakeError("persistency directory creation failed"));
  }

  const auto destination = directory / EncodedFileName(key);
  const auto temporary = directory / (EncodedFileName(key) + ".tmp");
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) {
    return core::Result<bool>::FromError(MakeError("persistency temporary file did not open"));
  }

  output << kFileMagic << '\n';
  output << "schema=" << entry.schema_version << '\n';
  output << "key=" << key << '\n';
  output << "size=" << entry.value.size() << '\n';
  output << "checksum=" << entry.checksum << '\n';
  output << '\n';
  output.write(
    reinterpret_cast<const char*>(entry.value.data()),
    static_cast<std::streamsize>(entry.value.size()));
  output.close();
  if (!output) {
    return core::Result<bool>::FromError(MakeError("persistency temporary file write failed"));
  }

  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return core::Result<bool>::FromError(MakeError("persistency atomic rename failed"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] bool IsUnsafeBackupPath(const std::filesystem::path& path) {
  return path.empty() || path == path.root_path();
}

}  // namespace

core::Result<KeyValueStore> KeyValueStore::OpenInMemory(StoreConfig config) {
  auto validation = ValidateConfig(config, BackendKind::kInMemory);
  if (!validation) {
    return core::Result<KeyValueStore>::FromError(validation.Error());
  }

  return core::Result<KeyValueStore>::FromValue(
    KeyValueStore(std::move(config), BackendKind::kInMemory));
}

core::Result<KeyValueStore> KeyValueStore::OpenFilesystem(StoreConfig config) {
  auto validation = ValidateConfig(config, BackendKind::kFilesystem);
  if (!validation) {
    return core::Result<KeyValueStore>::FromError(validation.Error());
  }

  KeyValueStore store(std::move(config), BackendKind::kFilesystem);
  auto loaded = store.LoadFilesystem();
  if (!loaded) {
    return core::Result<KeyValueStore>::FromError(loaded.Error());
  }

  return core::Result<KeyValueStore>::FromValue(std::move(store));
}

core::Result<bool> KeyValueStore::Put(
  std::string_view key,
  std::span<const std::uint8_t> value) {
  auto writable = EnsureWritable();
  if (!writable) {
    return core::Result<bool>::FromError(writable.Error());
  }

  auto key_validation = ValidateKey(key);
  if (!key_validation) {
    return core::Result<bool>::FromError(key_validation.Error());
  }

  if (value.empty()) {
    return core::Result<bool>::FromError(MakeError("persistency value is empty"));
  }

  auto quota = EnsureQuota(key, value.size());
  if (!quota) {
    return core::Result<bool>::FromError(quota.Error());
  }

  Entry entry{
    .schema_version = config_.schema_version,
    .value = std::vector<std::uint8_t>(value.begin(), value.end()),
    .checksum = Checksum(value),
  };

  if (backend_ == BackendKind::kFilesystem) {
    auto written = WriteEntryFile(key, entry);
    if (!written) {
      return core::Result<bool>::FromError(written.Error());
    }
  }

  entries_[std::string(key)] = std::move(entry);
  return core::Result<bool>::FromValue(true);
}

core::Result<std::vector<std::uint8_t>> KeyValueStore::Get(std::string_view key) const {
  auto key_validation = ValidateKey(key);
  if (!key_validation) {
    return core::Result<std::vector<std::uint8_t>>::FromError(key_validation.Error());
  }

  const auto iter = entries_.find(std::string(key));
  if (iter == entries_.end()) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("persistency key is missing"));
  }

  return core::Result<std::vector<std::uint8_t>>::FromValue(iter->second.value);
}

core::Result<bool> KeyValueStore::Remove(std::string_view key) {
  auto writable = EnsureWritable();
  if (!writable) {
    return core::Result<bool>::FromError(writable.Error());
  }

  if (!config_.access.allow_remove) {
    return core::Result<bool>::FromError(MakeError("persistency remove is denied by policy"));
  }

  auto key_validation = ValidateKey(key);
  if (!key_validation) {
    return core::Result<bool>::FromError(key_validation.Error());
  }

  const auto erased = entries_.erase(std::string(key));
  if (erased == 0U) {
    return core::Result<bool>::FromError(MakeError("persistency key is missing"));
  }

  if (backend_ == BackendKind::kFilesystem) {
    auto removed = RemoveEntryFile(key);
    if (!removed) {
      return core::Result<bool>::FromError(removed.Error());
    }
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> KeyValueStore::MigrateSchema(std::uint32_t next_schema_version) {
  auto writable = EnsureWritable();
  if (!writable) {
    return core::Result<bool>::FromError(writable.Error());
  }

  if (next_schema_version <= config_.schema_version) {
    return core::Result<bool>::FromError(MakeError("schema migration is not forward-only"));
  }

  config_.schema_version = next_schema_version;
  for (auto& [_, entry] : entries_) {
    entry.schema_version = next_schema_version;
  }

  return Flush();
}

core::Result<bool> KeyValueStore::BackupTo(
  const std::filesystem::path& backup_directory) const {
  if (IsUnsafeBackupPath(backup_directory)) {
    return core::Result<bool>::FromError(MakeError("backup path is unsafe"));
  }

  std::error_code error;
  std::filesystem::remove_all(backup_directory, error);
  if (error) {
    return core::Result<bool>::FromError(MakeError("backup cleanup failed"));
  }

  std::filesystem::create_directories(backup_directory, error);
  if (error) {
    return core::Result<bool>::FromError(MakeError("backup directory creation failed"));
  }

  for (const auto& [key, entry] : entries_) {
    auto written = WriteEntryFileToDirectory(backup_directory, key, entry);
    if (!written) {
      return core::Result<bool>::FromError(written.Error());
    }
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> KeyValueStore::RestoreFrom(
  const std::filesystem::path& backup_directory) {
  auto writable = EnsureWritable();
  if (!writable) {
    return core::Result<bool>::FromError(writable.Error());
  }

  std::error_code error;
  if (!std::filesystem::is_directory(backup_directory, error) || error) {
    return core::Result<bool>::FromError(MakeError("backup directory is not readable"));
  }

  std::unordered_map<std::string, Entry> restored;
  std::size_t restored_bytes{0U};
  std::uint32_t restored_schema{config_.schema_version};
  for (const auto& file : std::filesystem::directory_iterator(backup_directory, error)) {
    if (error) {
      return core::Result<bool>::FromError(MakeError("backup directory iteration failed"));
    }

    if (!file.is_regular_file() || !HasOakvExtension(file.path())) {
      continue;
    }

    auto decoded = ReadEntryFile(file.path());
    if (!decoded) {
      return core::Result<bool>::FromError(decoded.Error());
    }

    auto key_validation = ValidateKey(decoded.Value().key);
    if (!key_validation) {
      return core::Result<bool>::FromError(key_validation.Error());
    }

    restored_bytes += decoded.Value().entry.value.size();
    if (restored_bytes > config_.quota_bytes) {
      return core::Result<bool>::FromError(MakeError("restored data exceeds quota"));
    }

    restored_schema = std::max(restored_schema, decoded.Value().entry.schema_version);
    restored.emplace(std::move(decoded.Value().key), std::move(decoded.Value().entry));
  }

  entries_ = std::move(restored);
  config_.schema_version = restored_schema;
  return Flush();
}

core::Result<bool> KeyValueStore::ImportFile(
  std::string_view key,
  const std::filesystem::path& source) {
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    return core::Result<bool>::FromError(MakeError("source file did not open"));
  }

  std::vector<std::uint8_t> value{
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>()};
  return Put(key, value);
}

core::Result<bool> KeyValueStore::ExportFile(
  std::string_view key,
  const std::filesystem::path& destination) const {
  auto value = Get(key);
  if (!value) {
    return core::Result<bool>::FromError(value.Error());
  }

  std::error_code error;
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    return core::Result<bool>::FromError(MakeError("export directory creation failed"));
  }

  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) {
    return core::Result<bool>::FromError(MakeError("export file did not open"));
  }

  output.write(
    reinterpret_cast<const char*>(value.Value().data()),
    static_cast<std::streamsize>(value.Value().size()));
  output.close();
  if (!output) {
    return core::Result<bool>::FromError(MakeError("export file write failed"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> KeyValueStore::Flush() const {
  if (backend_ == BackendKind::kInMemory) {
    return core::Result<bool>::FromValue(true);
  }

  for (const auto& [key, entry] : entries_) {
    auto written = WriteEntryFile(key, entry);
    if (!written) {
      return core::Result<bool>::FromError(written.Error());
    }
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> KeyValueStore::Shutdown() {
  auto flushed = Flush();
  if (!flushed) {
    return core::Result<bool>::FromError(flushed.Error());
  }

  entries_.clear();
  return core::Result<bool>::FromValue(true);
}

StoreSnapshot KeyValueStore::Snapshot() const {
  StoreSnapshot snapshot{
    .backend = backend_,
    .health = health_,
    .schema_version = config_.schema_version,
    .used_bytes = 0U,
    .quota_bytes = config_.quota_bytes,
    .recovered_entries = recovered_entries_,
    .entries = {},
  };

  snapshot.entries.reserve(entries_.size());
  for (const auto& [key, entry] : entries_) {
    snapshot.used_bytes += entry.value.size();
    snapshot.entries.push_back({
      .key = key,
      .schema_version = entry.schema_version,
      .size = entry.value.size(),
      .checksum = entry.checksum,
    });
  }

  std::sort(
    snapshot.entries.begin(),
    snapshot.entries.end(),
    [](const auto& left, const auto& right) { return left.key < right.key; });
  return snapshot;
}

KeyValueStore::KeyValueStore(StoreConfig config, BackendKind backend) noexcept
  : config_(std::move(config)), backend_(backend) {}

core::Result<bool> KeyValueStore::ValidateKey(std::string_view key) const {
  return ValidateKeyForPolicy(key, config_.access.key_prefix);
}

core::Result<bool> KeyValueStore::EnsureWritable() const {
  if (config_.access.read_only) {
    return core::Result<bool>::FromError(MakeError("persistency store is read-only"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> KeyValueStore::EnsureQuota(
  std::string_view key,
  std::size_t replacement_size) const {
  std::size_t used{0U};
  for (const auto& [stored_key, entry] : entries_) {
    if (stored_key != key) {
      used += entry.value.size();
    }
  }

  if (used + replacement_size > config_.quota_bytes) {
    return core::Result<bool>::FromError(MakeError("persistency quota exceeded"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> KeyValueStore::WriteEntryFile(std::string_view key, const Entry& entry) const {
  return WriteEntryFileToDirectory(StoreDirectory(), key, entry);
}

core::Result<bool> KeyValueStore::RemoveEntryFile(std::string_view key) const {
  std::error_code error;
  std::filesystem::remove(StoreDirectory() / EncodedFileName(key), error);
  if (error) {
    return core::Result<bool>::FromError(MakeError("persistency file removal failed"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> KeyValueStore::LoadFilesystem() {
  std::error_code error;
  std::filesystem::create_directories(StoreDirectory(), error);
  if (error) {
    return core::Result<bool>::FromError(MakeError("persistency directory creation failed"));
  }

  std::size_t used_bytes{0U};
  for (const auto& file : std::filesystem::directory_iterator(StoreDirectory(), error)) {
    if (error) {
      return core::Result<bool>::FromError(MakeError("persistency directory iteration failed"));
    }

    if (!file.is_regular_file() || !HasOakvExtension(file.path())) {
      continue;
    }

    auto decoded = ReadEntryFile(file.path());
    if (!decoded) {
      auto quarantine = file.path();
      quarantine += ".corrupt";
      std::filesystem::remove(quarantine, error);
      std::filesystem::rename(file.path(), quarantine, error);
      if (error) {
        return core::Result<bool>::FromError(MakeError("persistency quarantine failed"));
      }
      ++recovered_entries_;
      health_ = StoreHealth::kRecovered;
      continue;
    }

    auto key_validation = ValidateKey(decoded.Value().key);
    if (!key_validation) {
      return core::Result<bool>::FromError(key_validation.Error());
    }

    used_bytes += decoded.Value().entry.value.size();
    if (used_bytes > config_.quota_bytes) {
      return core::Result<bool>::FromError(MakeError("stored data exceeds quota"));
    }

    entries_.emplace(std::move(decoded.Value().key), std::move(decoded.Value().entry));
  }

  return core::Result<bool>::FromValue(true);
}

std::filesystem::path KeyValueStore::StoreDirectory() const {
  return config_.root / config_.application_id;
}

core::Result<RemotePersistencyClient> RemotePersistencyClient::Create(
  RemotePersistencyPolicy policy,
  RemotePersistencyTarget target) {
  auto policy_validation = ValidateRemotePolicy(policy);
  if (!policy_validation) {
    return core::Result<RemotePersistencyClient>::FromError(policy_validation.Error());
  }

  auto target_validation = ValidateRemoteTarget(target);
  if (!target_validation) {
    return core::Result<RemotePersistencyClient>::FromError(target_validation.Error());
  }

  auto report = BuildRemoteValidationReport(policy, target);
  return core::Result<RemotePersistencyClient>::FromValue(
    RemotePersistencyClient(std::move(policy), std::move(target), std::move(report)));
}

core::Result<RemoteValidationReport> RemotePersistencyClient::ValidateConnection() const {
  return core::Result<RemoteValidationReport>::FromValue(validation_);
}

core::Result<RemoteOperationReceipt> RemotePersistencyClient::MirrorPut(
  std::string_view key,
  std::span<const std::uint8_t> value,
  std::uint32_t schema_version,
  std::uint64_t monotonic_ms) {
  auto receipt = MakeReceipt(
    RemoteOperation::kMirrorPut,
    key,
    schema_version,
    value.size(),
    Checksum(value),
    monotonic_ms);

  auto operation_validation = ValidateOperation(
    RemoteOperation::kMirrorPut,
    key,
    schema_version,
    value.size());
  if (!operation_validation) {
    return BlockReceipt(std::move(receipt), operation_validation.Error());
  }

  receipt.accepted = true;
  receipt.sequence = ++sequence_;
  ++accepted_operations_;
  receipts_.push_back(receipt);
  return core::Result<RemoteOperationReceipt>::FromValue(std::move(receipt));
}

core::Result<RemoteOperationReceipt> RemotePersistencyClient::RequestRestore(
  std::string_view key,
  std::uint32_t expected_schema_version,
  std::uint64_t monotonic_ms) {
  auto receipt = MakeReceipt(
    RemoteOperation::kRestoreRequest,
    key,
    expected_schema_version,
    0U,
    0U,
    monotonic_ms);

  auto operation_validation = ValidateOperation(
    RemoteOperation::kRestoreRequest,
    key,
    expected_schema_version,
    0U);
  if (!operation_validation) {
    return BlockReceipt(std::move(receipt), operation_validation.Error());
  }

  receipt.accepted = true;
  receipt.sequence = ++sequence_;
  ++accepted_operations_;
  receipts_.push_back(receipt);
  return core::Result<RemoteOperationReceipt>::FromValue(std::move(receipt));
}

RemotePersistencySnapshot RemotePersistencyClient::Snapshot() const {
  return {
    .validation = validation_,
    .accepted_operations = accepted_operations_,
    .blocked_operations = blocked_operations_,
    .receipts = receipts_,
  };
}

RemotePersistencyClient::RemotePersistencyClient(
  RemotePersistencyPolicy policy,
  RemotePersistencyTarget target,
  RemoteValidationReport validation) noexcept
  : policy_(std::move(policy)),
    target_(std::move(target)),
    validation_(std::move(validation)) {}

core::Result<bool> RemotePersistencyClient::ValidateOperation(
  RemoteOperation operation,
  std::string_view key,
  std::uint32_t schema_version,
  std::size_t value_size) const {
  if (!validation_.accepted) {
    return core::Result<bool>::FromError(
      MakeError("remote target weakens required guarantees"));
  }

  auto key_validation = ValidateKeyForPolicy(key, policy_.key_prefix);
  if (!key_validation) {
    return core::Result<bool>::FromError(key_validation.Error());
  }

  if (schema_version != policy_.schema_version) {
    return core::Result<bool>::FromError(MakeError("remote schema version mismatch"));
  }

  if (operation == RemoteOperation::kMirrorPut && !policy_.allow_remote_writes) {
    return core::Result<bool>::FromError(MakeError("remote writes are disabled by policy"));
  }

  if (operation == RemoteOperation::kMirrorPut && value_size == 0U) {
    return core::Result<bool>::FromError(MakeError("remote persistency value is empty"));
  }

  if (operation == RemoteOperation::kMirrorPut && value_size > policy_.quota_bytes) {
    return core::Result<bool>::FromError(MakeError("remote persistency quota exceeded"));
  }

  return core::Result<bool>::FromValue(true);
}

RemoteOperationReceipt RemotePersistencyClient::MakeReceipt(
  RemoteOperation operation,
  std::string_view key,
  std::uint32_t schema_version,
  std::size_t value_size,
  std::uint32_t checksum,
  std::uint64_t monotonic_ms) const {
  return {
    .accepted = false,
    .operation = operation,
    .endpoint = target_.endpoint,
    .key = std::string(key),
    .schema_version = schema_version,
    .value_size = value_size,
    .checksum = checksum,
    .sequence = 0U,
    .monotonic_ms = monotonic_ms,
    .audit_label = policy_.audit_label,
  };
}

core::Result<RemoteOperationReceipt> RemotePersistencyClient::BlockReceipt(
  RemoteOperationReceipt receipt,
  core::ErrorCode error) {
  receipt.sequence = ++sequence_;
  ++blocked_operations_;
  receipts_.push_back(std::move(receipt));
  return core::Result<RemoteOperationReceipt>::FromError(std::move(error));
}

std::string_view ToString(BackendKind backend) noexcept {
  switch (backend) {
    case BackendKind::kInMemory:
      return "InMemory";
    case BackendKind::kFilesystem:
      return "Filesystem";
  }

  return "Unknown";
}

std::string_view ToString(StoreHealth health) noexcept {
  switch (health) {
    case StoreHealth::kHealthy:
      return "Healthy";
    case StoreHealth::kRecovered:
      return "Recovered";
  }

  return "Unknown";
}

std::string_view ToString(RemoteTransport transport) noexcept {
  switch (transport) {
    case RemoteTransport::kMutualTls:
      return "MutualTls";
    case RemoteTransport::kLocalIpcGateway:
      return "LocalIpcGateway";
    case RemoteTransport::kTestLoopback:
      return "TestLoopback";
  }

  return "Unknown";
}

std::string_view ToString(RemoteGuarantee guarantee) noexcept {
  switch (guarantee) {
    case RemoteGuarantee::kConfidentiality:
      return "Confidentiality";
    case RemoteGuarantee::kIntegrity:
      return "Integrity";
    case RemoteGuarantee::kAvailability:
      return "Availability";
    case RemoteGuarantee::kRollbackProtection:
      return "RollbackProtection";
    case RemoteGuarantee::kDataResidency:
      return "DataResidency";
    case RemoteGuarantee::kLatency:
      return "Latency";
    case RemoteGuarantee::kMutualAuthentication:
      return "MutualAuthentication";
  }

  return "Unknown";
}

std::string_view ToString(RemoteOperation operation) noexcept {
  switch (operation) {
    case RemoteOperation::kMirrorPut:
      return "MirrorPut";
    case RemoteOperation::kRestoreRequest:
      return "RestoreRequest";
  }

  return "Unknown";
}

}  // namespace openautosar::runtime::persistency
