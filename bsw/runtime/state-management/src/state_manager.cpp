// SPDX-License-Identifier: MIT

#include "openautosar/runtime/state_manager.h"

#include <algorithm>
#include <utility>

namespace openautosar::runtime::state {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"state-management", message};
}

[[nodiscard]] bool ContainsState(
  const std::vector<FunctionGroupState>& states,
  FunctionGroupState state) {
  return std::find(states.begin(), states.end(), state) != states.end();
}

[[nodiscard]] const FunctionGroupTransition* FindTransition(
  const FunctionGroupDefinition& definition,
  FunctionGroupState from,
  FunctionGroupState to) {
  const auto iter = std::find_if(
    definition.transitions.begin(),
    definition.transitions.end(),
    [from, to](const FunctionGroupTransition& transition) {
      return transition.from == from && transition.to == to;
    });
  if (iter == definition.transitions.end()) {
    return nullptr;
  }

  return &(*iter);
}

}  // namespace

StateManager::StateManager(ExecutionManager& execution) : execution_(execution) {}

core::Result<bool> StateManager::RegisterFunctionGroup(
  FunctionGroupDefinition definition) {
  auto validation = ValidateDefinition(definition);
  if (!validation) {
    return validation;
  }

  const auto duplicate = std::find_if(
    function_groups_.begin(),
    function_groups_.end(),
    [&definition](const FunctionGroupRecord& item) {
      return item.definition.name == definition.name;
    });
  if (duplicate != function_groups_.end()) {
    return core::Result<bool>::FromError(
      MakeError("function group is already registered"));
  }

  if (function_groups_.empty() && execution_.FunctionGroup() != definition.initial_state) {
    return core::Result<bool>::FromError(
      MakeError("initial state does not match Execution Management"));
  }

  function_groups_.push_back(FunctionGroupRecord{
    .definition = std::move(definition),
    .state = function_groups_.empty()
      ? execution_.FunctionGroup()
      : function_groups_.back().state,
  });
  function_groups_.back().state = function_groups_.back().definition.initial_state;
  return core::Result<bool>::FromValue(true);
}

core::Result<std::uint64_t> StateManager::SubmitStateRequest(StateRequest request) {
  auto validation = ValidateRequest(request);
  if (!validation) {
    return core::Result<std::uint64_t>::FromError(validation.Error());
  }

  auto group = FindFunctionGroup(request.function_group);
  if (!group) {
    return core::Result<std::uint64_t>::FromError(group.Error());
  }

  const auto sequence = next_sequence_++;
  pending_requests_.push_back(PendingRequest{
    .sequence = sequence,
    .request = std::move(request),
  });
  return core::Result<std::uint64_t>::FromValue(sequence);
}

core::Result<TransitionRecord> StateManager::ApplyNextRequest() {
  if (pending_requests_.empty()) {
    return core::Result<TransitionRecord>::FromError(
      MakeError("no pending state request is available"));
  }

  const auto iter = std::max_element(
    pending_requests_.begin(),
    pending_requests_.end(),
    [](const PendingRequest& left, const PendingRequest& right) {
      const auto left_priority = static_cast<std::uint8_t>(left.request.priority);
      const auto right_priority = static_cast<std::uint8_t>(right.request.priority);
      if (left_priority != right_priority) {
        return left_priority < right_priority;
      }

      return left.sequence > right.sequence;
    });

  PendingRequest selected = std::move(*iter);
  pending_requests_.erase(iter);
  return ApplyRequest(std::move(selected.request), selected.sequence);
}

core::Result<TransitionRecord> StateManager::RequestFunctionGroupState(
  StateRequest request) {
  auto validation = ValidateRequest(request);
  if (!validation) {
    return core::Result<TransitionRecord>::FromError(validation.Error());
  }

  return ApplyRequest(std::move(request), next_sequence_++);
}

core::Result<FunctionGroupState> StateManager::CurrentState(
  std::string_view function_group) const {
  auto group = FindFunctionGroup(function_group);
  if (!group) {
    return core::Result<FunctionGroupState>::FromError(group.Error());
  }

  return core::Result<FunctionGroupState>::FromValue(group.Value()->state);
}

std::vector<StateRequest> StateManager::PendingRequests() const {
  std::vector<StateRequest> result;
  result.reserve(pending_requests_.size());
  for (const auto& pending : pending_requests_) {
    result.push_back(pending.request);
  }
  return result;
}

core::Result<bool> StateManager::RegisterObserver(TransitionObserver observer) {
  if (!observer) {
    return core::Result<bool>::FromError(MakeError("transition observer is invalid"));
  }

  observers_.push_back(std::move(observer));
  return core::Result<bool>::FromValue(true);
}

core::Result<StateManager::FunctionGroupRecord*> StateManager::FindFunctionGroup(
  std::string_view function_group) {
  const auto iter = std::find_if(
    function_groups_.begin(),
    function_groups_.end(),
    [function_group](const FunctionGroupRecord& item) {
      return item.definition.name == function_group;
    });
  if (iter == function_groups_.end()) {
    return core::Result<FunctionGroupRecord*>::FromError(
      MakeError("function group is not registered"));
  }

  return core::Result<FunctionGroupRecord*>::FromValue(&(*iter));
}

core::Result<const StateManager::FunctionGroupRecord*> StateManager::FindFunctionGroup(
  std::string_view function_group) const {
  const auto iter = std::find_if(
    function_groups_.begin(),
    function_groups_.end(),
    [function_group](const FunctionGroupRecord& item) {
      return item.definition.name == function_group;
    });
  if (iter == function_groups_.end()) {
    return core::Result<const FunctionGroupRecord*>::FromError(
      MakeError("function group is not registered"));
  }

  return core::Result<const FunctionGroupRecord*>::FromValue(&(*iter));
}

core::Result<TransitionRecord> StateManager::ApplyRequest(
  StateRequest request,
  std::uint64_t sequence) {
  auto group = FindFunctionGroup(request.function_group);
  if (!group) {
    return core::Result<TransitionRecord>::FromError(group.Error());
  }

  auto& function_group = *group.Value();
  TransitionRecord record;
  record.sequence = sequence;
  record.request = std::move(request);
  record.previous_state = function_group.state;
  record.resulting_state = function_group.state;

  if (record.request.requested_state == function_group.state) {
    record.accepted = true;
    record.completed = true;
    StoreRecord(record);
    return core::Result<TransitionRecord>::FromValue(history_.back());
  }

  const auto* transition = FindTransition(
    function_group.definition,
    function_group.state,
    record.request.requested_state);
  if (transition == nullptr) {
    record.failure = "function group transition is not allowed";
    StoreRecord(record);
    return core::Result<TransitionRecord>::FromError(MakeError(record.failure.c_str()));
  }

  record.accepted = true;
  for (const auto& action : transition->process_actions) {
    auto action_result = ApplyProcessAction(action);
    if (!action_result) {
      record.failure = action_result.Error().message;
      StoreRecord(record);
      return core::Result<TransitionRecord>::FromError(action_result.Error());
    }

    record.actions.push_back(action_result.Value());
    if (!action_result.Value().success && action.required) {
      record.failure = action_result.Value().error;
      auto degraded = EnterFailureState(function_group, *transition, record);
      if (!degraded) {
        record.failure = degraded.Error().message;
      }
      StoreRecord(record);
      return core::Result<TransitionRecord>::FromError(MakeError(record.failure.c_str()));
    }
  }

  auto execution_transition =
    execution_.TransitionFunctionGroup(record.request.requested_state);
  if (!execution_transition) {
    record.failure = execution_transition.Error().message;
    auto degraded = EnterFailureState(function_group, *transition, record);
    if (!degraded) {
      record.failure = degraded.Error().message;
    }
    StoreRecord(record);
    return core::Result<TransitionRecord>::FromError(MakeError(record.failure.c_str()));
  }

  function_group.state = execution_transition.Value();
  record.resulting_state = function_group.state;
  record.completed = true;
  StoreRecord(record);
  return core::Result<TransitionRecord>::FromValue(history_.back());
}

core::Result<ProcessActionReport> StateManager::ApplyProcessAction(
  const ProcessTransitionAction& action) {
  ProcessActionReport report;
  report.process_name = action.process_name;
  report.action = action.action;
  report.required = action.required;

  core::Result<ProcessState> result =
    core::Result<ProcessState>::FromValue(ProcessState::kDeclared);
  switch (action.action) {
    case ProcessTransitionActionKind::kStart:
      result = execution_.StartProcess(action.process_name);
      break;
    case ProcessTransitionActionKind::kStop:
      result = execution_.StopProcess(action.process_name);
      break;
    case ProcessTransitionActionKind::kRestart: {
      auto stopped = execution_.StopProcess(action.process_name);
      if (!stopped) {
        report.error = stopped.Error().message;
        return core::Result<ProcessActionReport>::FromValue(report);
      }
      result = execution_.StartProcess(action.process_name);
      break;
    }
  }

  if (!result) {
    report.error = result.Error().message;
    return core::Result<ProcessActionReport>::FromValue(report);
  }

  report.success = true;
  report.resulting_state = result.Value();
  return core::Result<ProcessActionReport>::FromValue(report);
}

core::Result<bool> StateManager::EnterFailureState(
  FunctionGroupRecord& function_group,
  const FunctionGroupTransition& transition,
  TransitionRecord& record) {
  if (!transition.enter_degraded_on_failure) {
    return core::Result<bool>::FromValue(false);
  }

  if (!ContainsState(function_group.definition.allowed_states, transition.failure_state)) {
    return core::Result<bool>::FromError(MakeError("failure state is not allowed"));
  }

  if (function_group.state == transition.failure_state) {
    record.resulting_state = function_group.state;
    return core::Result<bool>::FromValue(true);
  }

  auto degraded = execution_.TransitionFunctionGroup(transition.failure_state);
  if (!degraded) {
    return core::Result<bool>::FromError(degraded.Error());
  }

  function_group.state = degraded.Value();
  record.resulting_state = function_group.state;
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> StateManager::ValidateDefinition(
  const FunctionGroupDefinition& definition) const {
  if (definition.name.empty()) {
    return core::Result<bool>::FromError(MakeError("function group name is empty"));
  }

  if (definition.allowed_states.empty()) {
    return core::Result<bool>::FromError(MakeError("function group has no states"));
  }

  if (!ContainsState(definition.allowed_states, definition.initial_state)) {
    return core::Result<bool>::FromError(
      MakeError("initial state is not part of the function group"));
  }

  if (definition.transitions.empty()) {
    return core::Result<bool>::FromError(
      MakeError("function group has no transitions"));
  }

  for (const auto& transition : definition.transitions) {
    if (!ContainsState(definition.allowed_states, transition.from) ||
        !ContainsState(definition.allowed_states, transition.to)) {
      return core::Result<bool>::FromError(
        MakeError("transition references an unknown state"));
    }

    if (transition.enter_degraded_on_failure &&
        !ContainsState(definition.allowed_states, transition.failure_state)) {
      return core::Result<bool>::FromError(
        MakeError("transition failure state is unknown"));
    }

    for (const auto& action : transition.process_actions) {
      if (action.process_name.empty()) {
        return core::Result<bool>::FromError(
          MakeError("transition process action has no process"));
      }
    }
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> StateManager::ValidateRequest(const StateRequest& request) const {
  if (request.requester.empty()) {
    return core::Result<bool>::FromError(MakeError("state requester is empty"));
  }

  if (request.function_group.empty()) {
    return core::Result<bool>::FromError(MakeError("state request has no function group"));
  }

  if (request.reason.empty()) {
    return core::Result<bool>::FromError(MakeError("state request has no reason"));
  }

  return core::Result<bool>::FromValue(true);
}

void StateManager::StoreRecord(TransitionRecord record) {
  history_.push_back(std::move(record));
  NotifyObservers(history_.back());
}

void StateManager::NotifyObservers(const TransitionRecord& record) const {
  for (const auto& observer : observers_) {
    observer(record);
  }
}

std::string_view ToString(StateRequestPriority priority) noexcept {
  switch (priority) {
    case StateRequestPriority::kLow:
      return "Low";
    case StateRequestPriority::kNormal:
      return "Normal";
    case StateRequestPriority::kHigh:
      return "High";
    case StateRequestPriority::kCritical:
      return "Critical";
  }

  return "Unknown";
}

std::string_view ToString(ProcessTransitionActionKind action) noexcept {
  switch (action) {
    case ProcessTransitionActionKind::kStart:
      return "Start";
    case ProcessTransitionActionKind::kStop:
      return "Stop";
    case ProcessTransitionActionKind::kRestart:
      return "Restart";
  }

  return "Unknown";
}

}  // namespace openautosar::runtime::state
