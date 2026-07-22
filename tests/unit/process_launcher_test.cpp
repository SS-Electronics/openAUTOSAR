// SPDX-License-Identifier: MIT

#include "openautosar/platform/os/process/process_launcher.h"
#include "openautosar/runtime/execution_manager.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
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

bool Contains(
  const std::vector<std::string>& values,
  std::string_view expected) {
  for (const auto& value : values) {
    if (value == expected) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  namespace launcher = openautosar::platform::os::process;
  namespace runtime = openautosar::runtime;

  launcher::DryRunProcessLauncher dry_run;
  Require(!dry_run.Launch({}).HasValue(), "invalid launch spec was accepted");

  launcher::ProcessLaunchSpec spec;
  spec.process_name = "ultrasonic-provider";
  spec.executable = "/usr/bin/oa-ultrasonic-provider";
  spec.arguments = {"--manifest", "/etc/openautosar/ultrasonic.json"};
  spec.environment = {
    {.name = "OA_LOG_LEVEL", .value = "info"},
    {.name = "OA_MACHINE", .value = "qemux86-64"},
  };
  spec.user = "openautosar";
  spec.group = "openautosar";
  spec.working_directory = "/var/lib/openautosar";
  spec.security_label = "openautosar.ultrasonic";
  spec.policy.cgroup.memory_max_bytes = 67'108'864U;
  spec.policy.cgroup.cpu_weight = 250U;
  spec.policy.cgroup.allowed_cpus = {0U, 1U};
  spec.policy.cgroup.file_descriptor_limit = 256U;
  spec.policy.namespaces.private_network = false;
  spec.policy.capabilities.permitted = {"CAP_NET_RAW"};
  spec.policy.seccomp.profile = "openautosar-default";
  spec.startup_timeout = std::chrono::milliseconds{750};

  auto launched = dry_run.Launch(spec);
  Require(launched.HasValue(), "valid launch spec failed");
  Require(launched.Value().pid == 1000U, "dry-run pid seed changed");
  Require(
    launched.Value().scope_name == "openautosar-ultrasonic-provider.scope",
    "scope name changed");
  Require(
    Contains(launched.Value().systemd_properties, "MemoryMax=67108864"),
    "memory limit was not mapped to systemd");
  Require(
    Contains(launched.Value().systemd_properties, "CPUWeight=250"),
    "CPU weight was not mapped to systemd");
  Require(
    Contains(launched.Value().systemd_properties, "CapabilityBoundingSet=CAP_NET_RAW"),
    "capability policy was not mapped to systemd");
  Require(
    Contains(launched.Value().systemd_properties, "SystemCallFilter=@system-service"),
    "seccomp syscall group was not mapped to systemd");
  Require(
    Contains(
      launched.Value().cgroup_operations,
      "write /sys/fs/cgroup/openautosar.slice/"
      "openautosar-ultrasonic-provider.scope/cpuset.cpus 0,1"),
    "CPU set cgroup operation was not generated");
  Require(launched.Value().resource_policy_applied, "resource policy was not applied");
  Require(launched.Value().initialized, "process was not marked initialized");

  auto stopped = dry_run.Stop("ultrasonic-provider");
  Require(stopped.HasValue(), "active process stop failed");
  Require(
    stopped.Value().state == launcher::LauncherProcessState::kTerminated,
    "stopped process did not terminate");
  Require(!dry_run.Stop("ultrasonic-provider").HasValue(), "inactive stop succeeded");

  launcher::DryRunProcessLauncher execution_launcher;
  runtime::ExecutionManager execution{execution_launcher};
  runtime::ProcessManifest manifest;
  manifest.name = "ultrasonic-provider";
  manifest.executable = "/usr/bin/oa-ultrasonic-provider";
  manifest.arguments = {"--samples", "4"};
  manifest.environment = {{.name = "OA_LOG_LEVEL", .value = "debug"}};
  manifest.working_directory = "/var/lib/openautosar";
  manifest.security_label = "openautosar.ultrasonic";
  manifest.resource_policy.max_restarts = 0U;
  manifest.resource_policy.startup_timeout = std::chrono::milliseconds{500};
  manifest.launch_policy.cgroup.memory_max_bytes = 33'554'432U;
  manifest.launch_policy.cgroup.cpu_weight = 300U;
  manifest.launch_policy.cgroup.file_descriptor_limit = 128U;
  manifest.launch_policy.namespaces.private_tmp = true;
  manifest.launch_policy.capabilities.permitted = {"CAP_NET_RAW"};

  Require(execution.RegisterProcess(manifest).HasValue(), "manifest registration failed");
  Require(execution.StartProcess(manifest.name).HasValue(), "process start failed");
  auto launch_record = execution.LastLaunchOf(manifest.name);
  Require(launch_record.HasValue(), "Execution Management has no launch record");
  Require(
    Contains(launch_record.Value().systemd_properties, "MemoryMax=33554432"),
    "Execution Management did not apply memory policy");
  Require(
    Contains(launch_record.Value().systemd_properties, "CPUWeight=300"),
    "Execution Management did not apply CPU policy");
  Require(
    launch_record.Value().argv.size() == 3U &&
      launch_record.Value().argv[1U] == "--samples",
    "Execution Management launch argv changed");
  Require(
    execution.StopProcess(manifest.name).Value() == runtime::ProcessState::kStopped,
    "process stop did not reach Stopped");
  Require(!execution.StartProcess(manifest.name).HasValue(), "restart budget was ignored");
  Require(
    execution.StateOf(manifest.name).Value() == runtime::ProcessState::kFailed,
    "restart budget exhaustion did not fail process");

  Require(
    launcher::ToString(launcher::LauncherProcessState::kRunning) ==
      std::string_view("Running"),
    "launcher state text changed");
  Require(
    runtime::ToString(runtime::ProcessState::kStarting) ==
      std::string_view("Starting"),
    "execution state text changed");

  return 0;
}
