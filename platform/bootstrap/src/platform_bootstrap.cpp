// SPDX-License-Identifier: MIT

#include "openautosar/platform/bootstrap.h"

namespace openautosar::platform {

PlatformBootstrap::PlatformBootstrap(log::Logger& logger)
  : logger_(logger), execution_(), state_(execution_) {}

core::Result<BootstrapReport> PlatformBootstrap::Run() {
  logger_.Info("starting platform bootstrap");

  runtime::ProcessManifest manifest{
    .name = "/OpenAUTOSAR/Examples/HelloService",
    .executable = "oa-hello-service",
    .arguments = {},
    .environment = {},
    .user = "openautosar",
    .group = "openautosar",
    .working_directory = "/",
    .security_label = "openautosar.example",
    .resource_policy = runtime::ResourcePolicy{.max_restarts = 1U},
    .launch_policy = {},
  };

  auto registered = execution_.RegisterProcess(manifest);
  if (!registered) {
    return core::Result<BootstrapReport>::FromError(registered.Error());
  }

  auto state_model = state_.RegisterFunctionGroup({
    .name = "MachineFG",
    .initial_state = runtime::FunctionGroupState::kOff,
    .allowed_states = {
      runtime::FunctionGroupState::kOff,
      runtime::FunctionGroupState::kStartup,
      runtime::FunctionGroupState::kDrivingReady,
      runtime::FunctionGroupState::kDegraded,
      runtime::FunctionGroupState::kShutdown,
    },
    .transitions = {
      {
        .from = runtime::FunctionGroupState::kOff,
        .to = runtime::FunctionGroupState::kStartup,
        .process_actions = {},
        .rationale = "platform prerequisites are available",
      },
      {
        .from = runtime::FunctionGroupState::kStartup,
        .to = runtime::FunctionGroupState::kDrivingReady,
        .process_actions = {{
          .process_name = manifest.name,
          .action = runtime::state::ProcessTransitionActionKind::kStart,
          .required = true,
        }},
        .enter_degraded_on_failure = true,
        .rationale = "reference Adaptive Application starts",
      },
      {
        .from = runtime::FunctionGroupState::kDrivingReady,
        .to = runtime::FunctionGroupState::kShutdown,
        .process_actions = {{
          .process_name = manifest.name,
          .action = runtime::state::ProcessTransitionActionKind::kStop,
          .required = false,
        }},
        .rationale = "platform shutdown",
      },
    },
  });
  if (!state_model) {
    return core::Result<BootstrapReport>::FromError(state_model.Error());
  }

  auto startup = state_.RequestFunctionGroupState({
    .requester = "platform-bootstrap",
    .function_group = "MachineFG",
    .requested_state = runtime::FunctionGroupState::kStartup,
    .priority = runtime::state::StateRequestPriority::kCritical,
    .reason = "bootstrap sequence",
  });
  if (!startup) {
    return core::Result<BootstrapReport>::FromError(startup.Error());
  }

  auto driving_ready = state_.RequestFunctionGroupState({
    .requester = "platform-bootstrap",
    .function_group = "MachineFG",
    .requested_state = runtime::FunctionGroupState::kDrivingReady,
    .priority = runtime::state::StateRequestPriority::kCritical,
    .reason = "start reference application",
  });
  if (!driving_ready) {
    return core::Result<BootstrapReport>::FromError(driving_ready.Error());
  }

  logger_.Info("platform bootstrap entered DrivingReady");

  BootstrapReport report;
  report.function_group = driving_ready.Value().resulting_state;
  for (const auto& action : driving_ready.Value().actions) {
    if (action.action == runtime::state::ProcessTransitionActionKind::kStart &&
        action.success) {
      report.started_processes.push_back(action.process_name);
    }
  }
  return core::Result<BootstrapReport>::FromValue(report);
}

}  // namespace openautosar::platform
