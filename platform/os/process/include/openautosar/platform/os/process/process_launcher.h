// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openautosar::platform::os::process {

enum class LauncherProcessState {
  kCreated,
  kRunning,
  kTerminated,
  kFailed,
};

struct EnvironmentVariable final {
  std::string name;
  std::string value;
};

struct CgroupPolicy final {
  std::string slice{"openautosar.slice"};
  std::string scope_prefix{"openautosar"};
  std::uint64_t memory_max_bytes{0U};
  std::uint32_t cpu_weight{100U};
  std::vector<std::uint32_t> allowed_cpus;
  std::uint32_t file_descriptor_limit{1024U};
};

struct NamespacePolicy final {
  bool private_tmp{true};
  bool private_devices{true};
  bool private_network{false};
  bool protect_system{true};
};

struct CapabilityPolicy final {
  std::vector<std::string> permitted;
  bool no_new_privileges{true};
};

struct SeccompPolicy final {
  std::string profile{"default"};
  std::vector<std::string> allowed_syscall_groups{"@system-service"};
};

struct LaunchPolicy final {
  CgroupPolicy cgroup;
  NamespacePolicy namespaces;
  CapabilityPolicy capabilities;
  SeccompPolicy seccomp;
};

struct ProcessLaunchSpec final {
  std::string process_name;
  std::string executable;
  std::vector<std::string> arguments;
  std::vector<EnvironmentVariable> environment;
  std::string user{"openautosar"};
  std::string group{"openautosar"};
  std::string working_directory{"/"};
  std::string security_label;
  LaunchPolicy policy;
  std::chrono::milliseconds startup_timeout{std::chrono::seconds(5)};
};

struct LaunchRecord final {
  std::uint64_t pid{0U};
  std::string process_name;
  LauncherProcessState state{LauncherProcessState::kCreated};
  std::string executable;
  std::vector<std::string> argv;
  std::vector<std::string> environment;
  std::string scope_name;
  std::vector<std::string> systemd_properties;
  std::vector<std::string> cgroup_operations;
  bool created{false};
  bool initialized{false};
  bool resource_policy_applied{false};
};

class IProcessLauncher {
public:
  IProcessLauncher() = default;
  IProcessLauncher(const IProcessLauncher&) = delete;
  IProcessLauncher& operator=(const IProcessLauncher&) = delete;
  virtual ~IProcessLauncher() = default;

  [[nodiscard]] virtual core::Result<LaunchRecord> Launch(
    ProcessLaunchSpec spec) = 0;
  [[nodiscard]] virtual core::Result<LaunchRecord> Stop(
    std::string_view process_name) = 0;
  [[nodiscard]] virtual core::Result<LaunchRecord> Status(
    std::string_view process_name) const = 0;
};

class DryRunProcessLauncher final : public IProcessLauncher {
public:
  [[nodiscard]] core::Result<LaunchRecord> Launch(ProcessLaunchSpec spec) override;
  [[nodiscard]] core::Result<LaunchRecord> Stop(
    std::string_view process_name) override;
  [[nodiscard]] core::Result<LaunchRecord> Status(
    std::string_view process_name) const override;

  [[nodiscard]] const std::vector<LaunchRecord>& Records() const noexcept {
    return records_;
  }

private:
  std::vector<LaunchRecord> records_;
  std::unordered_map<std::string, std::size_t> active_records_;
  std::uint64_t next_pid_{1000U};
};

[[nodiscard]] core::Result<bool> ValidateLaunchSpec(const ProcessLaunchSpec& spec);
[[nodiscard]] std::string ScopeNameFor(
  std::string_view process_name,
  std::string_view prefix);
[[nodiscard]] std::string_view ToString(LauncherProcessState state) noexcept;

}  // namespace openautosar::platform::os::process
