// SPDX-License-Identifier: MIT

#include "openautosar/runtime/persistency.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>(),
  };
}

bool HasRemoteGap(
  const openautosar::runtime::persistency::RemoteValidationReport& report,
  openautosar::runtime::persistency::RemoteGuarantee guarantee) {
  return std::any_of(report.weakened_guarantees.begin(), report.weakened_guarantees.end(),
                     [guarantee](const auto& gap) { return gap.guarantee == guarantee; });
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  namespace per = openautosar::runtime::persistency;

  const auto test_root = fs::temp_directory_path() / "openautosar-persistency-test";
  std::error_code error;
  fs::remove_all(test_root, error);
  fs::create_directories(test_root, error);
  Require(!error, "persistency test root setup failed");

  const auto invalid = per::KeyValueStore::OpenInMemory({});
  Require(!invalid.HasValue(), "invalid persistency config was accepted");

  auto memory = per::KeyValueStore::OpenInMemory({
    .application_id = "ultrasonic-provider",
    .schema_version = 1U,
    .quota_bytes = 16U,
    .access = {.key_prefix = "vehicle/"},
    .root = {},
  });
  Require(memory.HasValue(), "in-memory persistency store did not open");

  const std::vector<std::uint8_t> distance{0x01U, 0x2CU};
  auto stored = memory.Value().Put("vehicle/last-distance", distance);
  Require(stored.HasValue(), "persistency put failed");

  auto loaded = memory.Value().Get("vehicle/last-distance");
  Require(loaded.HasValue(), "persistency get failed");
  Require(loaded.Value() == distance, "persistency value roundtrip changed");

  const auto denied_key = memory.Value().Put("private/last-distance", distance);
  Require(!denied_key.HasValue(), "access policy allowed a denied persistency key");

  const std::vector<std::uint8_t> oversized(32U, 0xAAU);
  const auto over_quota = memory.Value().Put("vehicle/oversized", oversized);
  Require(!over_quota.HasValue(), "persistency quota violation was accepted");

  auto migrated = memory.Value().MigrateSchema(2U);
  Require(migrated.HasValue(), "persistency schema migration failed");
  auto snapshot = memory.Value().Snapshot();
  Require(snapshot.schema_version == 2U, "persistency schema version did not migrate");
  Require(snapshot.entries.size() == 1U, "persistency snapshot entry count changed");
  Require(snapshot.entries[0U].schema_version == 2U, "entry schema version did not migrate");

  const auto source_file = test_root / "source.bin";
  {
    std::ofstream source(source_file, std::ios::binary | std::ios::trunc);
    source.write("\x10\x20\x30", 3);
  }
  Require(memory.Value().ImportFile("vehicle/blob", source_file).HasValue(), "file import failed");

  const auto exported_file = test_root / "exported.bin";
  Require(
    memory.Value().ExportFile("vehicle/blob", exported_file).HasValue(),
    "file export failed");
  Require(ReadBinaryFile(exported_file) == std::vector<std::uint8_t>({0x10U, 0x20U, 0x30U}),
          "exported file contents changed");

  const auto backup_dir = test_root / "backup";
  Require(memory.Value().BackupTo(backup_dir).HasValue(), "persistency backup failed");

  auto restored = per::KeyValueStore::OpenInMemory({
    .application_id = "ultrasonic-provider",
    .schema_version = 1U,
    .quota_bytes = 64U,
    .access = {.key_prefix = "vehicle/"},
    .root = {},
  });
  Require(restored.HasValue(), "restore target did not open");
  Require(restored.Value().RestoreFrom(backup_dir).HasValue(), "persistency restore failed");
  const std::vector<std::uint8_t> expected_blob{0x10U, 0x20U, 0x30U};
  Require(
    restored.Value().Get("vehicle/blob").Value() == expected_blob,
    "restored file value changed");

  auto read_only = per::KeyValueStore::OpenInMemory({
    .application_id = "readonly",
    .schema_version = 1U,
    .quota_bytes = 16U,
    .access = {.key_prefix = "vehicle/", .read_only = true},
    .root = {},
  });
  Require(read_only.HasValue(), "read-only persistency store did not open");
  Require(!read_only.Value().Put("vehicle/value", distance).HasValue(), "read-only put succeeded");

  auto filesystem = per::KeyValueStore::OpenFilesystem({
    .application_id = "ultrasonic-provider",
    .schema_version = 1U,
    .quota_bytes = 64U,
    .access = {.key_prefix = "vehicle/"},
    .root = test_root / "store",
  });
  Require(filesystem.HasValue(), "filesystem persistency store did not open");
  Require(filesystem.Value().Put("vehicle/state", distance).HasValue(), "filesystem put failed");
  Require(filesystem.Value().Shutdown().HasValue(), "filesystem shutdown failed");

  auto reopened = per::KeyValueStore::OpenFilesystem({
    .application_id = "ultrasonic-provider",
    .schema_version = 1U,
    .quota_bytes = 64U,
    .access = {.key_prefix = "vehicle/"},
    .root = test_root / "store",
  });
  Require(reopened.HasValue(), "filesystem store did not reopen");
  Require(reopened.Value().Get("vehicle/state").Value() == distance, "filesystem value changed");

  const auto store_dir = test_root / "store" / "ultrasonic-provider";
  for (const auto& file : fs::directory_iterator(store_dir)) {
    if (file.path().extension() == ".oakv") {
      std::ofstream corrupt(file.path(), std::ios::binary | std::ios::trunc);
      corrupt << "corrupted";
      break;
    }
  }

  auto recovered = per::KeyValueStore::OpenFilesystem({
    .application_id = "ultrasonic-provider",
    .schema_version = 1U,
    .quota_bytes = 64U,
    .access = {.key_prefix = "vehicle/"},
    .root = test_root / "store",
  });
  Require(recovered.HasValue(), "filesystem recovery open failed");
  const auto recovered_snapshot = recovered.Value().Snapshot();
  Require(recovered_snapshot.health == per::StoreHealth::kRecovered, "corruption not recovered");
  Require(recovered_snapshot.recovered_entries == 1U, "recovered entry count changed");
  Require(!recovered.Value().Get("vehicle/state").HasValue(), "corrupt value remained readable");

  Require(
    per::ToString(per::BackendKind::kFilesystem) == std::string_view("Filesystem"),
    "persistency backend text changed");
  Require(
    per::ToString(per::StoreHealth::kRecovered) == std::string_view("Recovered"),
    "persistency health text changed");

  const per::RemotePersistencyPolicy remote_policy{
    .application_id = "ultrasonic-provider",
    .schema_version = 2U,
    .key_prefix = "vehicle/",
    .quota_bytes = 16U,
    .allow_remote_writes = true,
    .require_confidentiality = true,
    .require_integrity = true,
    .require_mutual_authentication = true,
    .require_rollback_protection = true,
    .min_availability_class = 3U,
    .required_data_residency_region = "us-east-1",
    .max_latency_ms = 50U,
    .audit_label = "persistency-shadow",
  };
  const per::RemotePersistencyTarget strong_target{
    .endpoint = "persistency://cluster-a/vehicle-shadow",
    .data_residency_region = "us-east-1",
    .transport = per::RemoteTransport::kMutualTls,
    .confidentiality_enabled = true,
    .integrity_enabled = true,
    .mutual_authentication = true,
    .rollback_protection_enabled = true,
    .availability_class = 3U,
    .advertised_latency_ms = 25U,
  };

  auto remote = per::RemotePersistencyClient::Create(remote_policy, strong_target);
  Require(remote.HasValue(), "remote persistency representation did not open");
  auto remote_report = remote.Value().ValidateConnection();
  Require(remote_report.HasValue(), "remote persistency validation failed");
  Require(remote_report.Value().accepted, "strong remote persistency target was rejected");
  Require(
    remote_report.Value().weakened_guarantees.empty(),
    "strong remote persistency target reported weakened guarantees");

  auto mirrored = remote.Value().MirrorPut("vehicle/last-distance", distance, 2U, 1100U);
  Require(mirrored.HasValue(), "remote persistency mirror put was rejected");
  Require(mirrored.Value().accepted, "remote persistency receipt was not accepted");
  Require(mirrored.Value().sequence == 1U, "remote persistency sequence changed");
  Require(mirrored.Value().value_size == distance.size(), "remote receipt size changed");
  Require(
    mirrored.Value().audit_label == "persistency-shadow",
    "remote receipt audit label changed");

  auto restore_request = remote.Value().RequestRestore("vehicle/last-distance", 2U, 1200U);
  Require(restore_request.HasValue(), "remote persistency restore request was rejected");
  Require(
    restore_request.Value().operation == per::RemoteOperation::kRestoreRequest,
    "remote restore operation kind changed");
  Require(restore_request.Value().sequence == 2U, "remote restore sequence changed");

  auto remote_snapshot = remote.Value().Snapshot();
  Require(remote_snapshot.accepted_operations == 2U, "accepted remote operation count changed");
  Require(remote_snapshot.blocked_operations == 0U, "remote operation was blocked unexpectedly");
  Require(remote_snapshot.receipts.size() == 2U, "remote receipt count changed");

  const per::RemotePersistencyTarget weak_target{
    .endpoint = "persistency://cluster-b/vehicle-shadow",
    .data_residency_region = "eu-central-1",
    .transport = per::RemoteTransport::kTestLoopback,
    .confidentiality_enabled = false,
    .integrity_enabled = false,
    .mutual_authentication = false,
    .rollback_protection_enabled = false,
    .availability_class = 1U,
    .advertised_latency_ms = 250U,
  };
  auto weak_remote = per::RemotePersistencyClient::Create(remote_policy, weak_target);
  Require(weak_remote.HasValue(), "weakened remote target could not be represented");
  const auto weak_report = weak_remote.Value().ValidateConnection();
  Require(weak_report.HasValue(), "weakened remote validation failed");
  Require(!weak_report.Value().accepted, "weakened remote target was accepted");
  Require(
    weak_report.Value().weakened_guarantees.size() == 7U,
    "weakened remote guarantee count changed");
  Require(
    HasRemoteGap(weak_report.Value(), per::RemoteGuarantee::kConfidentiality),
    "remote confidentiality weakening was not reported");
  Require(
    HasRemoteGap(weak_report.Value(), per::RemoteGuarantee::kIntegrity),
    "remote integrity weakening was not reported");
  Require(
    HasRemoteGap(weak_report.Value(), per::RemoteGuarantee::kMutualAuthentication),
    "remote mutual authentication weakening was not reported");
  Require(
    HasRemoteGap(weak_report.Value(), per::RemoteGuarantee::kRollbackProtection),
    "remote rollback weakening was not reported");
  Require(
    HasRemoteGap(weak_report.Value(), per::RemoteGuarantee::kAvailability),
    "remote availability weakening was not reported");
  Require(
    HasRemoteGap(weak_report.Value(), per::RemoteGuarantee::kDataResidency),
    "remote residency weakening was not reported");
  Require(
    HasRemoteGap(weak_report.Value(), per::RemoteGuarantee::kLatency),
    "remote latency weakening was not reported");

  const auto weak_mirror =
    weak_remote.Value().MirrorPut("vehicle/last-distance", distance, 2U, 2100U);
  Require(!weak_mirror.HasValue(), "weakened remote target allowed mirror put");
  const auto weak_snapshot = weak_remote.Value().Snapshot();
  Require(weak_snapshot.blocked_operations == 1U, "weakened remote block count changed");
  Require(!weak_snapshot.receipts[0U].accepted, "weakened remote receipt was accepted");

  auto write_disabled_policy = remote_policy;
  write_disabled_policy.allow_remote_writes = false;
  auto write_disabled =
    per::RemotePersistencyClient::Create(write_disabled_policy, strong_target);
  Require(write_disabled.HasValue(), "write-disabled remote representation failed");
  const auto denied_write =
    write_disabled.Value().MirrorPut("vehicle/last-distance", distance, 2U, 3100U);
  Require(!denied_write.HasValue(), "write-disabled remote policy allowed mirror put");
  Require(
    write_disabled.Value().RequestRestore("vehicle/last-distance", 2U, 3200U).HasValue(),
    "write-disabled remote policy rejected restore request");

  const auto schema_mismatch =
    remote.Value().MirrorPut("vehicle/schema-mismatch", distance, 1U, 4100U);
  Require(!schema_mismatch.HasValue(), "remote schema mismatch was accepted");
  const auto denied_remote_key =
    remote.Value().MirrorPut("private/last-distance", distance, 2U, 4200U);
  Require(!denied_remote_key.HasValue(), "remote key prefix violation was accepted");

  auto tight_quota_policy = remote_policy;
  tight_quota_policy.quota_bytes = 1U;
  auto tight_quota = per::RemotePersistencyClient::Create(tight_quota_policy, strong_target);
  Require(tight_quota.HasValue(), "tight quota remote representation failed");
  Require(
    !tight_quota.Value().MirrorPut("vehicle/last-distance", distance, 2U, 5100U).HasValue(),
    "remote quota violation was accepted");

  Require(
    per::ToString(per::RemoteTransport::kMutualTls) == std::string_view("MutualTls"),
    "remote transport text changed");
  Require(
    per::ToString(per::RemoteGuarantee::kLatency) == std::string_view("Latency"),
    "remote guarantee text changed");
  Require(
    per::ToString(per::RemoteOperation::kRestoreRequest) ==
      std::string_view("RestoreRequest"),
    "remote operation text changed");

  fs::remove_all(test_root, error);
  return 0;
}
