// SPDX-License-Identifier: MIT

#include "openautosar/platform/os/process/process_launcher.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace openautosar::platform::os::process {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"process-launcher", message};
}

[[nodiscard]] bool IsEnvironmentName(std::string_view value) noexcept {
  if (value.empty()) {
    return false;
  }

  const auto first = static_cast<unsigned char>(value.front());
  if (std::isalpha(first) == 0 && value.front() != '_') {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](char item) {
    const auto byte = static_cast<unsigned char>(item);
    return std::isalnum(byte) != 0 || item == '_';
  });
}

[[nodiscard]] bool IsCapability(std::string_view value) noexcept {
  return value.rfind("CAP_", 0U) == 0U && value.size() > 4U;
}

[[nodiscard]] std::string JoinCpuSet(const std::vector<std::uint32_t>& cpus) {
  std::ostringstream output;
  for (std::size_t index = 0U; index < cpus.size(); ++index) {
    if (index != 0U) {
      output << ',';
    }
    output << cpus[index];
  }
  return output.str();
}

[[nodiscard]] std::string BoolProperty(bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::vector<std::string> BuildArgv(const ProcessLaunchSpec& spec) {
  std::vector<std::string> argv;
  argv.reserve(spec.arguments.size() + 1U);
  argv.push_back(spec.executable);
  argv.insert(argv.end(), spec.arguments.begin(), spec.arguments.end());
  return argv;
}

[[nodiscard]] std::vector<std::string> BuildEnvironment(
  const ProcessLaunchSpec& spec) {
  std::vector<std::string> environment;
  environment.reserve(spec.environment.size());
  for (const auto& item : spec.environment) {
    environment.push_back(item.name + "=" + item.value);
  }
  std::sort(environment.begin(), environment.end());
  return environment;
}

[[nodiscard]] std::vector<std::string> BuildSystemdProperties(
  const ProcessLaunchSpec& spec,
  std::string_view scope_name) {
  std::vector<std::string> properties;
  properties.push_back("Scope=" + std::string(scope_name));
  properties.push_back("Description=openAUTOSAR " + spec.process_name);
  properties.push_back("User=" + spec.user);
  properties.push_back("Group=" + spec.group);
  properties.push_back("WorkingDirectory=" + spec.working_directory);
  properties.push_back("Slice=" + spec.policy.cgroup.slice);
  properties.push_back("CPUWeight=" + std::to_string(spec.policy.cgroup.cpu_weight));
  properties.push_back(
    "LimitNOFILE=" +
    std::to_string(spec.policy.cgroup.file_descriptor_limit));
  properties.push_back(
    "NoNewPrivileges=" +
    BoolProperty(spec.policy.capabilities.no_new_privileges));
  properties.push_back(
    "PrivateTmp=" +
    BoolProperty(spec.policy.namespaces.private_tmp));
  properties.push_back(
    "PrivateDevices=" +
    BoolProperty(spec.policy.namespaces.private_devices));
  properties.push_back(
    "PrivateNetwork=" +
    BoolProperty(spec.policy.namespaces.private_network));
  properties.push_back(
    "ProtectSystem=" +
    BoolProperty(spec.policy.namespaces.protect_system));
  properties.push_back("SeccompProfile=" + spec.policy.seccomp.profile);

  if (spec.policy.cgroup.memory_max_bytes != 0U) {
    properties.push_back(
      "MemoryMax=" +
      std::to_string(spec.policy.cgroup.memory_max_bytes));
  }

  if (!spec.policy.capabilities.permitted.empty()) {
    std::ostringstream capabilities;
    for (std::size_t index = 0U;
         index < spec.policy.capabilities.permitted.size();
         ++index) {
      if (index != 0U) {
        capabilities << ' ';
      }
      capabilities << spec.policy.capabilities.permitted[index];
    }
    properties.push_back("CapabilityBoundingSet=" + capabilities.str());
  }

  for (const auto& group : spec.policy.seccomp.allowed_syscall_groups) {
    properties.push_back("SystemCallFilter=" + group);
  }

  return properties;
}

[[nodiscard]] std::vector<std::string> BuildCgroupOperations(
  const ProcessLaunchSpec& spec,
  std::string_view scope_name) {
  const auto cgroup_path = "/sys/fs/cgroup/" + spec.policy.cgroup.slice +
                           "/" + std::string(scope_name);
  std::vector<std::string> operations;
  operations.push_back("mkdir " + cgroup_path);
  operations.push_back(
    "write " + cgroup_path + "/cpu.weight " +
    std::to_string(spec.policy.cgroup.cpu_weight));
  operations.push_back(
    "write " + cgroup_path + "/pids.max " +
    std::to_string(spec.policy.cgroup.file_descriptor_limit));

  if (spec.policy.cgroup.memory_max_bytes != 0U) {
    operations.push_back(
      "write " + cgroup_path + "/memory.max " +
      std::to_string(spec.policy.cgroup.memory_max_bytes));
  }

  if (!spec.policy.cgroup.allowed_cpus.empty()) {
    operations.push_back(
      "write " + cgroup_path + "/cpuset.cpus " +
      JoinCpuSet(spec.policy.cgroup.allowed_cpus));
  }

  return operations;
}

}  // namespace

core::Result<LaunchRecord> DryRunProcessLauncher::Launch(ProcessLaunchSpec spec) {
  auto validation = ValidateLaunchSpec(spec);
  if (!validation) {
    return core::Result<LaunchRecord>::FromError(validation.Error());
  }

  const auto scope_name = ScopeNameFor(
    spec.process_name,
    spec.policy.cgroup.scope_prefix);
  LaunchRecord record;
  record.pid = next_pid_++;
  record.process_name = spec.process_name;
  record.state = LauncherProcessState::kRunning;
  record.executable = spec.executable;
  record.argv = BuildArgv(spec);
  record.environment = BuildEnvironment(spec);
  record.scope_name = scope_name;
  record.systemd_properties = BuildSystemdProperties(spec, scope_name);
  record.cgroup_operations = BuildCgroupOperations(spec, scope_name);
  record.created = true;
  record.initialized = true;
  record.resource_policy_applied = true;

  records_.push_back(std::move(record));
  active_records_[spec.process_name] = records_.size() - 1U;
  return core::Result<LaunchRecord>::FromValue(records_.back());
}

core::Result<LaunchRecord> DryRunProcessLauncher::Stop(
  std::string_view process_name) {
  const auto iter = active_records_.find(std::string(process_name));
  if (iter == active_records_.end()) {
    return core::Result<LaunchRecord>::FromError(
      MakeError("process is not active"));
  }

  auto& record = records_[iter->second];
  record.state = LauncherProcessState::kTerminated;
  active_records_.erase(iter);
  return core::Result<LaunchRecord>::FromValue(record);
}

core::Result<LaunchRecord> DryRunProcessLauncher::Status(
  std::string_view process_name) const {
  const auto iter = std::find_if(
    records_.rbegin(),
    records_.rend(),
    [process_name](const LaunchRecord& item) {
      return item.process_name == process_name;
    });
  if (iter == records_.rend()) {
    return core::Result<LaunchRecord>::FromError(
      MakeError("process has no launch record"));
  }

  return core::Result<LaunchRecord>::FromValue(*iter);
}

core::Result<bool> ValidateLaunchSpec(const ProcessLaunchSpec& spec) {
  if (spec.process_name.empty() || spec.executable.empty()) {
    return core::Result<bool>::FromError(
      MakeError("process name or executable is empty"));
  }

  if (spec.user.empty() || spec.group.empty()) {
    return core::Result<bool>::FromError(MakeError("process identity is incomplete"));
  }

  if (spec.working_directory.empty() || spec.working_directory.front() != '/') {
    return core::Result<bool>::FromError(
      MakeError("working directory must be absolute"));
  }

  if (spec.startup_timeout.count() <= 0) {
    return core::Result<bool>::FromError(MakeError("startup timeout is invalid"));
  }

  if (spec.policy.cgroup.slice.empty() || spec.policy.cgroup.scope_prefix.empty()) {
    return core::Result<bool>::FromError(MakeError("cgroup scope is incomplete"));
  }

  if (spec.policy.cgroup.cpu_weight < 1U ||
      spec.policy.cgroup.cpu_weight > 10'000U) {
    return core::Result<bool>::FromError(MakeError("CPU weight is outside cgroup v2 range"));
  }

  if (spec.policy.cgroup.file_descriptor_limit == 0U) {
    return core::Result<bool>::FromError(MakeError("file descriptor limit is invalid"));
  }

  for (const auto& item : spec.environment) {
    if (!IsEnvironmentName(item.name)) {
      return core::Result<bool>::FromError(MakeError("environment key is invalid"));
    }
  }

  for (const auto& capability : spec.policy.capabilities.permitted) {
    if (!IsCapability(capability)) {
      return core::Result<bool>::FromError(MakeError("capability name is invalid"));
    }
  }

  return core::Result<bool>::FromValue(true);
}

std::string ScopeNameFor(std::string_view process_name, std::string_view prefix) {
  std::string scope;
  scope.reserve(prefix.size() + process_name.size() + 8U);
  scope.append(prefix);
  scope.push_back('-');
  for (const auto item : process_name) {
    const auto byte = static_cast<unsigned char>(item);
    if (std::isalnum(byte) != 0 || item == '_' || item == '-') {
      scope.push_back(item);
    } else {
      scope.push_back('-');
    }
  }
  scope.append(".scope");
  return scope;
}

std::string_view ToString(LauncherProcessState state) noexcept {
  switch (state) {
    case LauncherProcessState::kCreated:
      return "Created";
    case LauncherProcessState::kRunning:
      return "Running";
    case LauncherProcessState::kTerminated:
      return "Terminated";
    case LauncherProcessState::kFailed:
      return "Failed";
  }

  return "Unknown";
}

}  // namespace openautosar::platform::os::process
