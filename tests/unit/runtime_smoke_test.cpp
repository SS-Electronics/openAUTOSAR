// SPDX-License-Identifier: MIT

#include "openautosar/core/instance_specifier.h"
#include "openautosar/log/logger.h"
#include "openautosar/platform/bootstrap.h"
#include "openautosar/runtime/execution_manager.h"

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

}  // namespace

int main() {
  const auto specifier =
    openautosar::core::InstanceSpecifier::Create("/OpenAUTOSAR/Machine/qemux86-64");
  Require(specifier.HasValue(), "valid instance specifier was rejected");

  const auto invalid_specifier =
    openautosar::core::InstanceSpecifier::Create("OpenAUTOSAR Machine");
  Require(!invalid_specifier.HasValue(), "invalid instance specifier was accepted");

  openautosar::runtime::ExecutionManager execution;
  const auto invalid_process = execution.RegisterProcess({});
  Require(!invalid_process.HasValue(), "invalid process manifest was accepted");

  const openautosar::runtime::ProcessManifest manifest{
    .name = "/OpenAUTOSAR/Tests/Process",
    .executable = "oa-test-process",
    .arguments = {},
    .environment = {},
    .user = "openautosar",
    .group = "openautosar",
    .working_directory = "/",
    .security_label = "openautosar.test",
    .resource_policy = {},
    .launch_policy = {},
  };

  const auto registered = execution.RegisterProcess(manifest);
  Require(registered.HasValue(), "valid process manifest was rejected");

  const auto startup =
    execution.TransitionFunctionGroup(openautosar::runtime::FunctionGroupState::kStartup);
  Require(startup.HasValue(), "startup transition failed");

  const auto running = execution.StartProcess(manifest.name);
  Require(running.HasValue(), "process did not enter running state");
  Require(running.Value() == openautosar::runtime::ProcessState::kRunning,
          "process state is not running");

  const auto ready =
    execution.TransitionFunctionGroup(openautosar::runtime::FunctionGroupState::kDrivingReady);
  Require(ready.HasValue(), "DrivingReady transition failed");

  openautosar::log::Logger logger("test.bootstrap");
  logger.SetSink([](const openautosar::log::Record&) {});

  openautosar::platform::PlatformBootstrap bootstrap(logger);
  const auto report = bootstrap.Run();
  Require(report.HasValue(), "platform bootstrap failed");
  Require(report.Value().function_group == openautosar::runtime::FunctionGroupState::kDrivingReady,
          "bootstrap did not reach DrivingReady");
  Require(report.Value().started_processes.size() == 1U, "bootstrap process count changed");

  return 0;
}
