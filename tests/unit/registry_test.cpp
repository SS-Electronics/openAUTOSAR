// SPDX-License-Identifier: MIT

#include "openautosar/runtime/registry_manager.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

openautosar::runtime::registry::RegistryRecord ServiceRecord() {
  namespace reg = openautosar::runtime::registry;
  return {
    .record_id = "svc.ultrasonic.front-center",
    .kind = reg::RegistryKind::kService,
    .state = reg::RegistryState::kActive,
    .version = "1.0.0",
    .owner = "oa-ultrasonic-gateway-smoke",
    .endpoint = "local://ultrasonic",
    .source_model = "model/examples/vehicle/ultrasonic_service.json",
    .last_update_ms = 100U,
    .lease_ttl_ms = 50U,
  };
}

openautosar::runtime::registry::RegistryRecord ProcessRecord() {
  namespace reg = openautosar::runtime::registry;
  return {
    .record_id = "process.oa-dashboard",
    .kind = reg::RegistryKind::kProcess,
    .state = reg::RegistryState::kDeclared,
    .version = "0.1.0",
    .owner = "execution-management",
    .endpoint = "pid://pending",
    .source_model = "deployment/machine/qemux86-64-agl-unagi.yaml",
    .last_update_ms = 90U,
    .lease_ttl_ms = 100U,
  };
}

}  // namespace

int main() {
  namespace reg = openautosar::runtime::registry;

  reg::RegistryManager manager{{.max_records = 3U}};
  Require(!manager.Register({}).HasValue(), "empty registry record was accepted");
  Require(manager.Snapshot().rejected_updates == 1U, "rejected update count changed");

  auto service = manager.Register(ServiceRecord());
  Require(service.HasValue(), "service registry record was rejected");
  Require(service.Value().state == reg::RegistryState::kActive, "service state changed");

  auto process = manager.Register(ProcessRecord());
  Require(process.HasValue(), "process registry record was rejected");
  auto active_process = manager.UpdateState(
    "process.oa-dashboard",
    reg::RegistryKind::kProcess,
    reg::RegistryState::kActive,
    110U);
  Require(active_process.HasValue(), "process state update failed");

  auto lookup = manager.Lookup("svc.ultrasonic.front-center", reg::RegistryKind::kService);
  Require(lookup.has_value(), "service lookup failed");
  Require(lookup->owner == "oa-ultrasonic-gateway-smoke", "service owner changed");

  auto active_records = manager.Query({
    .kind = std::nullopt,
    .state = reg::RegistryState::kActive,
    .owner_prefix = {},
    .record_prefix = {},
  });
  Require(active_records.size() == 2U, "active registry query size changed");
  Require(
    active_records[0U].record_id == "process.oa-dashboard",
    "registry query ordering changed");

  auto conflicting = ServiceRecord();
  conflicting.owner = "unexpected-owner";
  Require(
    !manager.Register(conflicting).HasValue(),
    "conflicting active registry record was accepted");

  manager.MarkStale(151U);
  auto snapshot = manager.Snapshot();
  Require(snapshot.stale_records == 1U, "stale registry count changed");
  Require(snapshot.active_records == 1U, "active registry count after stale mark changed");

  auto stale_service = manager.Lookup(
    "svc.ultrasonic.front-center",
    reg::RegistryKind::kService);
  Require(stale_service.has_value(), "stale service lookup failed");
  Require(stale_service->state == reg::RegistryState::kStale, "service was not marked stale");

  auto retired = manager.Retire(
    "process.oa-dashboard",
    reg::RegistryKind::kProcess,
    180U);
  Require(retired.HasValue(), "registry retire failed");
  Require(retired.Value().state == reg::RegistryState::kRetired, "retire state changed");
  Require(
    !manager.UpdateState(
       "process.oa-dashboard",
       reg::RegistryKind::kProcess,
       reg::RegistryState::kActive,
       190U)
       .HasValue(),
    "retired registry record was reactivated");

  reg::RegistryManager capacity{{.max_records = 1U, .require_source_model = false}};
  auto first = ServiceRecord();
  first.source_model.clear();
  Require(capacity.Register(first).HasValue(), "first capacity record failed");
  auto second = ProcessRecord();
  second.source_model.clear();
  Require(!capacity.Register(second).HasValue(), "registry capacity overflow was accepted");

  reg::RegistryManager strict{{.require_source_model = true}};
  auto missing_source = ServiceRecord();
  missing_source.source_model.clear();
  Require(!strict.Register(missing_source).HasValue(), "missing source model was accepted");

  Require(reg::ToString(reg::RegistryKind::kSoftwareCluster) ==
            std::string_view("SoftwareCluster"),
          "registry kind text changed");
  Require(reg::ToString(reg::RegistryState::kStale) == std::string_view("Stale"),
          "registry state text changed");

  return 0;
}
