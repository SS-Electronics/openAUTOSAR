// SPDX-License-Identifier: MIT

#include "openautosar/runtime/execution_manager.h"

#include <utility>

namespace openautosar::runtime {
namespace {

namespace process = platform::os::process;

[[nodiscard]] bool IsValidProcessManifest(const ProcessManifest& manifest) noexcept {
  return !manifest.name.empty() && !manifest.executable.empty() &&
         !manifest.user.empty() && !manifest.group.empty() &&
         !manifest.working_directory.empty() &&
         manifest.resource_policy.startup_timeout.count() > 0;
}

[[nodiscard]] process::ProcessLaunchSpec ToLaunchSpec(
  const ProcessManifest& manifest) {
  return {
    .process_name = manifest.name,
    .executable = manifest.executable,
    .arguments = manifest.arguments,
    .environment = manifest.environment,
    .user = manifest.user,
    .group = manifest.group,
    .working_directory = manifest.working_directory,
    .security_label = manifest.security_label,
    .policy = manifest.launch_policy,
    .startup_timeout = manifest.resource_policy.startup_timeout,
  };
}

[[nodiscard]] bool IsAllowedTransition(
  FunctionGroupState current,
  FunctionGroupState next) noexcept {
  if (next == current) {
    return true;
  }

  if (next == FunctionGroupState::kShutdown) {
    return true;
  }

  switch (current) {
    case FunctionGroupState::kOff:
      return next == FunctionGroupState::kStartup;
    case FunctionGroupState::kStartup:
      return next == FunctionGroupState::kDrivingReady ||
             next == FunctionGroupState::kDiagnostic ||
             next == FunctionGroupState::kDegraded;
    case FunctionGroupState::kDrivingReady:
      return next == FunctionGroupState::kParking || next == FunctionGroupState::kDegraded;
    case FunctionGroupState::kDiagnostic:
      return next == FunctionGroupState::kParking;
    case FunctionGroupState::kParking:
      return next == FunctionGroupState::kDrivingReady ||
             next == FunctionGroupState::kDiagnostic ||
             next == FunctionGroupState::kUpdate;
    case FunctionGroupState::kUpdate:
      return next == FunctionGroupState::kParking;
    case FunctionGroupState::kDegraded:
      return next == FunctionGroupState::kDrivingReady ||
             next == FunctionGroupState::kParking;
    case FunctionGroupState::kShutdown:
      return false;
  }

  return false;
}

}  // namespace

ExecutionManager::ExecutionManager() : default_launcher_(), launcher_(&default_launcher_) {}

ExecutionManager::ExecutionManager(process::IProcessLauncher& launcher)
  : default_launcher_(), launcher_(&launcher) {}

core::Result<std::string> ExecutionManager::RegisterProcess(ProcessManifest manifest) {
  if (!IsValidProcessManifest(manifest)) {
    return core::Result<std::string>::FromError(
      {"execution-management", "process manifest is incomplete"});
  }

  const auto name = manifest.name;
  if (processes_.find(name) != processes_.end()) {
    return core::Result<std::string>::FromError(
      {"execution-management", "process is already registered"});
  }

  processes_.emplace(
    name,
    ProcessRecord{
      .manifest = std::move(manifest),
      .state = ProcessState::kDeclared,
      .restart_count = 0U,
      .last_launch = std::nullopt,
    });
  return core::Result<std::string>::FromValue(name);
}

core::Result<ProcessState> ExecutionManager::StartProcess(std::string_view name) {
  auto record = FindMutable(name);
  if (!record) {
    return core::Result<ProcessState>::FromError(record.Error());
  }

  auto& process = *record.Value();
  if (process.state == ProcessState::kRunning) {
    return core::Result<ProcessState>::FromValue(ProcessState::kRunning);
  }

  if (process.restart_count > process.manifest.resource_policy.max_restarts) {
    process.state = ProcessState::kFailed;
    return core::Result<ProcessState>::FromError(
      {"execution-management", "process restart budget exhausted"});
  }

  ++process.restart_count;
  process.state = ProcessState::kStarting;
  auto launched = launcher_->Launch(ToLaunchSpec(process.manifest));
  if (!launched) {
    process.state = ProcessState::kFailed;
    return core::Result<ProcessState>::FromError(launched.Error());
  }

  process.last_launch = launched.Value();
  process.state = ProcessState::kRunning;
  return core::Result<ProcessState>::FromValue(process.state);
}

core::Result<ProcessState> ExecutionManager::StopProcess(std::string_view name) {
  auto record = FindMutable(name);
  if (!record) {
    return core::Result<ProcessState>::FromError(record.Error());
  }

  auto& process = *record.Value();
  if (process.state == ProcessState::kDeclared || process.state == ProcessState::kStopped) {
    process.state = ProcessState::kStopped;
    return core::Result<ProcessState>::FromValue(process.state);
  }

  process.state = ProcessState::kTerminating;
  if (process.last_launch.has_value()) {
    auto stopped = launcher_->Stop(process.manifest.name);
    if (!stopped) {
      process.state = ProcessState::kFailed;
      return core::Result<ProcessState>::FromError(stopped.Error());
    }
    process.last_launch = stopped.Value();
  }

  process.state = ProcessState::kStopped;
  return core::Result<ProcessState>::FromValue(process.state);
}

core::Result<FunctionGroupState> ExecutionManager::TransitionFunctionGroup(
  FunctionGroupState next) {
  if (!IsAllowedTransition(function_group_, next)) {
    return core::Result<FunctionGroupState>::FromError(
      {"execution-management", "function group transition is not allowed"});
  }

  function_group_ = next;
  return core::Result<FunctionGroupState>::FromValue(function_group_);
}

core::Result<ProcessState> ExecutionManager::StateOf(std::string_view name) const {
  auto record = Find(name);
  if (!record) {
    return core::Result<ProcessState>::FromError(record.Error());
  }

  return core::Result<ProcessState>::FromValue(record.Value()->state);
}

core::Result<LaunchRecord> ExecutionManager::LastLaunchOf(std::string_view name) const {
  auto record = Find(name);
  if (!record) {
    return core::Result<LaunchRecord>::FromError(record.Error());
  }

  if (!record.Value()->last_launch.has_value()) {
    return core::Result<LaunchRecord>::FromError(
      {"execution-management", "process has no launch record"});
  }

  return core::Result<LaunchRecord>::FromValue(*record.Value()->last_launch);
}

core::Result<ExecutionManager::ProcessRecord*> ExecutionManager::FindMutable(
  std::string_view name) {
  const auto iter = processes_.find(std::string(name));
  if (iter == processes_.end()) {
    return core::Result<ProcessRecord*>::FromError(
      {"execution-management", "process is not registered"});
  }

  return core::Result<ProcessRecord*>::FromValue(&iter->second);
}

core::Result<const ExecutionManager::ProcessRecord*> ExecutionManager::Find(
  std::string_view name) const {
  const auto iter = processes_.find(std::string(name));
  if (iter == processes_.end()) {
    return core::Result<const ProcessRecord*>::FromError(
      {"execution-management", "process is not registered"});
  }

  return core::Result<const ProcessRecord*>::FromValue(&iter->second);
}

std::string_view ToString(ProcessState state) noexcept {
  switch (state) {
    case ProcessState::kDeclared:
      return "Declared";
    case ProcessState::kCreated:
      return "Created";
    case ProcessState::kStarting:
      return "Starting";
    case ProcessState::kRunning:
      return "Running";
    case ProcessState::kTerminating:
      return "Terminating";
    case ProcessState::kStopped:
      return "Stopped";
    case ProcessState::kRestartPending:
      return "RestartPending";
    case ProcessState::kDegraded:
      return "Degraded";
    case ProcessState::kFailed:
      return "Failed";
  }

  return "Unknown";
}

std::string_view ToString(FunctionGroupState state) noexcept {
  switch (state) {
    case FunctionGroupState::kOff:
      return "Off";
    case FunctionGroupState::kStartup:
      return "Startup";
    case FunctionGroupState::kDrivingReady:
      return "DrivingReady";
    case FunctionGroupState::kDiagnostic:
      return "Diagnostic";
    case FunctionGroupState::kParking:
      return "Parking";
    case FunctionGroupState::kUpdate:
      return "Update";
    case FunctionGroupState::kDegraded:
      return "Degraded";
    case FunctionGroupState::kShutdown:
      return "Shutdown";
  }

  return "Unknown";
}

}  // namespace openautosar::runtime
