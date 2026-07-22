// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/platform/os/process/process_launcher.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openautosar::runtime {

enum class ProcessState {
  kDeclared,
  kCreated,
  kStarting,
  kRunning,
  kTerminating,
  kStopped,
  kRestartPending,
  kDegraded,
  kFailed,
};

enum class FunctionGroupState {
  kOff,
  kStartup,
  kDrivingReady,
  kDiagnostic,
  kParking,
  kUpdate,
  kDegraded,
  kShutdown,
};

struct ResourcePolicy final {
  std::uint32_t max_restarts{1U};
  std::chrono::milliseconds startup_timeout{std::chrono::seconds(5)};
};

using EnvironmentVariable = platform::os::process::EnvironmentVariable;
using LaunchPolicy = platform::os::process::LaunchPolicy;
using LaunchRecord = platform::os::process::LaunchRecord;

struct ProcessManifest final {
  std::string name;
  std::string executable;
  std::vector<std::string> arguments;
  std::vector<EnvironmentVariable> environment;
  std::string user{"openautosar"};
  std::string group{"openautosar"};
  std::string working_directory{"/"};
  std::string security_label;
  ResourcePolicy resource_policy{};
  LaunchPolicy launch_policy{};
};

class ExecutionManager final {
public:
  ExecutionManager();
  explicit ExecutionManager(platform::os::process::IProcessLauncher& launcher);

  [[nodiscard]] core::Result<std::string> RegisterProcess(ProcessManifest manifest);
  [[nodiscard]] core::Result<ProcessState> StartProcess(std::string_view name);
  [[nodiscard]] core::Result<ProcessState> StopProcess(std::string_view name);
  [[nodiscard]] core::Result<FunctionGroupState> TransitionFunctionGroup(FunctionGroupState next);

  [[nodiscard]] core::Result<ProcessState> StateOf(std::string_view name) const;
  [[nodiscard]] core::Result<LaunchRecord> LastLaunchOf(std::string_view name) const;
  [[nodiscard]] FunctionGroupState FunctionGroup() const noexcept { return function_group_; }

private:
  struct ProcessRecord final {
    ProcessManifest manifest;
    ProcessState state{ProcessState::kDeclared};
    std::uint32_t restart_count{0U};
    std::optional<LaunchRecord> last_launch;
  };

  [[nodiscard]] core::Result<ProcessRecord*> FindMutable(std::string_view name);
  [[nodiscard]] core::Result<const ProcessRecord*> Find(std::string_view name) const;

  std::unordered_map<std::string, ProcessRecord> processes_;
  FunctionGroupState function_group_{FunctionGroupState::kOff};
  platform::os::process::DryRunProcessLauncher default_launcher_;
  platform::os::process::IProcessLauncher* launcher_{nullptr};
};

[[nodiscard]] std::string_view ToString(ProcessState state) noexcept;
[[nodiscard]] std::string_view ToString(FunctionGroupState state) noexcept;

}  // namespace openautosar::runtime
