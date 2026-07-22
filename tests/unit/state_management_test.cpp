// SPDX-License-Identifier: MIT

#include "openautosar/runtime/execution_manager.h"
#include "openautosar/runtime/state_manager.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

openautosar::runtime::ProcessManifest Manifest(
  std::string name,
  std::string executable) {
  return {
    .name = std::move(name),
    .executable = std::move(executable),
    .arguments = {},
    .environment = {},
    .user = "openautosar",
    .group = "openautosar",
    .working_directory = "/",
    .security_label = "openautosar.test",
    .resource_policy = openautosar::runtime::ResourcePolicy{.max_restarts = 4U},
    .launch_policy = {},
  };
}

openautosar::runtime::state::FunctionGroupDefinition ReferenceModel() {
  namespace runtime = openautosar::runtime;
  namespace state = openautosar::runtime::state;

  return {
    .name = "MachineFG",
    .initial_state = runtime::FunctionGroupState::kOff,
    .allowed_states = {
      runtime::FunctionGroupState::kOff,
      runtime::FunctionGroupState::kStartup,
      runtime::FunctionGroupState::kDrivingReady,
      runtime::FunctionGroupState::kDiagnostic,
      runtime::FunctionGroupState::kParking,
      runtime::FunctionGroupState::kUpdate,
      runtime::FunctionGroupState::kDegraded,
      runtime::FunctionGroupState::kShutdown,
    },
    .transitions = {
      {
        .from = runtime::FunctionGroupState::kOff,
        .to = runtime::FunctionGroupState::kStartup,
        .process_actions = {},
        .rationale = "boot prerequisites",
      },
      {
        .from = runtime::FunctionGroupState::kStartup,
        .to = runtime::FunctionGroupState::kDrivingReady,
        .process_actions = {
          {
            .process_name = "ultrasonic-provider",
            .action = state::ProcessTransitionActionKind::kStart,
            .required = true,
          },
          {
            .process_name = "dashboard",
            .action = state::ProcessTransitionActionKind::kStart,
            .required = true,
          },
        },
        .enter_degraded_on_failure = true,
        .failure_state = runtime::FunctionGroupState::kDegraded,
        .rationale = "reference services available",
      },
      {
        .from = runtime::FunctionGroupState::kStartup,
        .to = runtime::FunctionGroupState::kDegraded,
        .process_actions = {},
        .rationale = "startup degraded fallback",
      },
      {
        .from = runtime::FunctionGroupState::kStartup,
        .to = runtime::FunctionGroupState::kDiagnostic,
        .process_actions = {},
        .rationale = "diagnostic boot mode",
      },
      {
        .from = runtime::FunctionGroupState::kDrivingReady,
        .to = runtime::FunctionGroupState::kDegraded,
        .process_actions = {
          {
            .process_name = "ultrasonic-provider",
            .action = state::ProcessTransitionActionKind::kRestart,
            .required = true,
          },
          {
            .process_name = "dashboard",
            .action = state::ProcessTransitionActionKind::kStop,
            .required = false,
          },
        },
        .rationale = "fault containment",
      },
      {
        .from = runtime::FunctionGroupState::kDegraded,
        .to = runtime::FunctionGroupState::kParking,
        .process_actions = {{
          .process_name = "ultrasonic-provider",
          .action = state::ProcessTransitionActionKind::kStop,
          .required = false,
        }},
        .rationale = "park after degraded operation",
      },
      {
        .from = runtime::FunctionGroupState::kParking,
        .to = runtime::FunctionGroupState::kUpdate,
        .process_actions = {{
          .process_name = "ultrasonic-provider",
          .action = state::ProcessTransitionActionKind::kStop,
          .required = false,
        }},
        .rationale = "prepare update window",
      },
      {
        .from = runtime::FunctionGroupState::kUpdate,
        .to = runtime::FunctionGroupState::kParking,
        .process_actions = {{
          .process_name = "ultrasonic-provider",
          .action = state::ProcessTransitionActionKind::kStart,
          .required = true,
        }},
        .rationale = "leave update window",
      },
      {
        .from = runtime::FunctionGroupState::kParking,
        .to = runtime::FunctionGroupState::kDrivingReady,
        .process_actions = {{
          .process_name = "dashboard",
          .action = state::ProcessTransitionActionKind::kStart,
          .required = false,
        }},
        .rationale = "resume driving state",
      },
      {
        .from = runtime::FunctionGroupState::kParking,
        .to = runtime::FunctionGroupState::kShutdown,
        .process_actions = {
          {
            .process_name = "ultrasonic-provider",
            .action = state::ProcessTransitionActionKind::kStop,
            .required = false,
          },
          {
            .process_name = "dashboard",
            .action = state::ProcessTransitionActionKind::kStop,
            .required = false,
          },
        },
        .rationale = "controlled shutdown",
      },
    },
  };
}

openautosar::runtime::state::StateRequest Request(
  openautosar::runtime::FunctionGroupState state,
  std::string reason,
  openautosar::runtime::state::StateRequestPriority priority =
    openautosar::runtime::state::StateRequestPriority::kNormal) {
  return {
    .requester = "state-test",
    .function_group = "MachineFG",
    .requested_state = state,
    .priority = priority,
    .reason = std::move(reason),
  };
}

}  // namespace

int main() {
  namespace runtime = openautosar::runtime;
  namespace state = openautosar::runtime::state;

  runtime::ExecutionManager execution;
  Require(
    execution.RegisterProcess(Manifest("ultrasonic-provider", "oa-provider")).HasValue(),
    "provider manifest was rejected");
  Require(
    execution.RegisterProcess(Manifest("dashboard", "oa-dashboard")).HasValue(),
    "dashboard manifest was rejected");

  state::StateManager manager(execution);
  Require(!manager.RegisterFunctionGroup({}).HasValue(), "invalid state model was accepted");
  Require(manager.RegisterFunctionGroup(ReferenceModel()).HasValue(), "state model failed");
  Require(!manager.RegisterFunctionGroup(ReferenceModel()).HasValue(), "duplicate model passed");
  Require(!manager.RequestFunctionGroupState(Request(
            runtime::FunctionGroupState::kDrivingReady,
            "skip startup")).HasValue(),
          "invalid direct transition was accepted");
  Require(!manager.History().empty(), "rejected transition was not recorded");
  Require(!manager.History().back().accepted, "rejected transition was accepted");

  std::size_t observed{0U};
  Require(manager.RegisterObserver([&observed](const state::TransitionRecord& record) {
            if (record.completed) {
              ++observed;
            }
          }).HasValue(),
          "valid observer was rejected");

  auto startup = manager.RequestFunctionGroupState(
    Request(runtime::FunctionGroupState::kStartup, "boot"));
  Require(startup.HasValue() && startup.Value().completed, "startup transition failed");
  Require(
    execution.FunctionGroup() == runtime::FunctionGroupState::kStartup,
    "Execution Management did not enter Startup");

  auto ready = manager.RequestFunctionGroupState(
    Request(runtime::FunctionGroupState::kDrivingReady, "start services"));
  Require(ready.HasValue() && ready.Value().completed, "DrivingReady transition failed");
  Require(ready.Value().actions.size() == 2U, "DrivingReady process action count changed");
  Require(
    execution.StateOf("ultrasonic-provider").Value() == runtime::ProcessState::kRunning,
    "provider did not start");
  Require(
    execution.StateOf("dashboard").Value() == runtime::ProcessState::kRunning,
    "dashboard did not start");

  auto degraded = manager.RequestFunctionGroupState(
    Request(runtime::FunctionGroupState::kDegraded, "provider health fault"));
  Require(degraded.HasValue() && degraded.Value().completed, "degraded transition failed");
  Require(degraded.Value().actions.front().action == state::ProcessTransitionActionKind::kRestart,
          "restart action was not recorded");
  Require(
    execution.StateOf("dashboard").Value() == runtime::ProcessState::kStopped,
    "dashboard did not stop for degraded state");

  auto parking = manager.RequestFunctionGroupState(
    Request(runtime::FunctionGroupState::kParking, "park vehicle"));
  Require(parking.HasValue() && parking.Value().completed, "Parking transition failed");
  Require(
    execution.StateOf("ultrasonic-provider").Value() == runtime::ProcessState::kStopped,
    "provider did not stop in Parking");

  auto update = manager.RequestFunctionGroupState(
    Request(runtime::FunctionGroupState::kUpdate, "enter update window"));
  Require(update.HasValue() && update.Value().completed, "Update transition failed");

  auto parked_again = manager.RequestFunctionGroupState(
    Request(runtime::FunctionGroupState::kParking, "leave update window"));
  Require(parked_again.HasValue() && parked_again.Value().completed, "Update exit failed");
  Require(
    execution.StateOf("ultrasonic-provider").Value() == runtime::ProcessState::kRunning,
    "provider did not restart after update");

  auto shutdown = manager.RequestFunctionGroupState(
    Request(runtime::FunctionGroupState::kShutdown, "controlled shutdown"));
  Require(shutdown.HasValue() && shutdown.Value().completed, "shutdown transition failed");
  Require(observed >= 6U, "observer did not receive completed transitions");

  runtime::ExecutionManager arbitration_execution;
  state::StateManager arbitration(arbitration_execution);
  Require(arbitration.RegisterFunctionGroup(ReferenceModel()).HasValue(),
          "arbitration state model failed");
  Require(arbitration.RequestFunctionGroupState(
            Request(runtime::FunctionGroupState::kStartup, "boot")).HasValue(),
          "arbitration startup failed");
  Require(arbitration.SubmitStateRequest(Request(
            runtime::FunctionGroupState::kDrivingReady,
            "normal ready",
            state::StateRequestPriority::kLow)).HasValue(),
          "low-priority request was rejected");
  Require(arbitration.SubmitStateRequest(Request(
            runtime::FunctionGroupState::kDegraded,
            "critical fault",
            state::StateRequestPriority::kCritical)).HasValue(),
          "critical request was rejected");
  auto selected = arbitration.ApplyNextRequest();
  Require(selected.HasValue(), "queued request arbitration failed");
  Require(
    selected.Value().request.requested_state == runtime::FunctionGroupState::kDegraded,
    "highest-priority request was not selected");
  Require(arbitration.PendingRequests().size() == 1U, "pending request count changed");

  runtime::ExecutionManager failure_execution;
  state::StateManager failure(failure_execution);
  Require(failure.RegisterFunctionGroup(ReferenceModel()).HasValue(),
          "failure state model failed");
  Require(failure.RequestFunctionGroupState(
            Request(runtime::FunctionGroupState::kStartup, "boot")).HasValue(),
          "failure startup failed");
  auto failed = failure.RequestFunctionGroupState(
    Request(runtime::FunctionGroupState::kDrivingReady, "missing process"));
  Require(!failed.HasValue(), "missing required process did not fail transition");
  auto failure_state = failure.CurrentState("MachineFG");
  Require(failure_state.HasValue(), "failure state was unavailable");
  Require(
    failure_state.Value() == runtime::FunctionGroupState::kDegraded,
    "required action failure did not enter degraded state");
  Require(!failure.History().back().completed, "failed transition was marked complete");
  Require(!failure.History().back().actions.front().success, "failed action was not recorded");

  Require(
    runtime::ToString(runtime::FunctionGroupState::kUpdate) == std::string_view("Update"),
    "Function Group state text changed");
  Require(
    state::ToString(state::StateRequestPriority::kCritical) ==
      std::string_view("Critical"),
    "request priority text changed");
  Require(
    state::ToString(state::ProcessTransitionActionKind::kRestart) ==
      std::string_view("Restart"),
    "process action text changed");

  return 0;
}
