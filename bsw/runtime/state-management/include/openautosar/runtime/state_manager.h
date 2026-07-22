// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/runtime/execution_manager.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::runtime::state {

enum class StateRequestPriority : std::uint8_t {
  kLow,
  kNormal,
  kHigh,
  kCritical,
};

enum class ProcessTransitionActionKind {
  kStart,
  kStop,
  kRestart,
};

struct ProcessTransitionAction final {
  std::string process_name;
  ProcessTransitionActionKind action{ProcessTransitionActionKind::kStart};
  bool required{true};
};

struct FunctionGroupTransition final {
  FunctionGroupState from{FunctionGroupState::kOff};
  FunctionGroupState to{FunctionGroupState::kStartup};
  std::vector<ProcessTransitionAction> process_actions;
  bool enter_degraded_on_failure{false};
  FunctionGroupState failure_state{FunctionGroupState::kDegraded};
  std::string rationale;
};

struct FunctionGroupDefinition final {
  std::string name;
  FunctionGroupState initial_state{FunctionGroupState::kOff};
  std::vector<FunctionGroupState> allowed_states;
  std::vector<FunctionGroupTransition> transitions;
};

struct StateRequest final {
  std::string requester;
  std::string function_group;
  FunctionGroupState requested_state{FunctionGroupState::kStartup};
  StateRequestPriority priority{StateRequestPriority::kNormal};
  std::string reason;
};

struct ProcessActionReport final {
  std::string process_name;
  ProcessTransitionActionKind action{ProcessTransitionActionKind::kStart};
  bool required{true};
  bool success{false};
  ProcessState resulting_state{ProcessState::kDeclared};
  std::string error;
};

struct TransitionRecord final {
  std::uint64_t sequence{0U};
  StateRequest request;
  FunctionGroupState previous_state{FunctionGroupState::kOff};
  FunctionGroupState resulting_state{FunctionGroupState::kOff};
  bool accepted{false};
  bool completed{false};
  std::string failure;
  std::vector<ProcessActionReport> actions;
};

using TransitionObserver = std::function<void(const TransitionRecord&)>;

class StateManager final {
public:
  explicit StateManager(ExecutionManager& execution);

  [[nodiscard]] core::Result<bool> RegisterFunctionGroup(
    FunctionGroupDefinition definition);
  [[nodiscard]] core::Result<std::uint64_t> SubmitStateRequest(StateRequest request);
  [[nodiscard]] core::Result<TransitionRecord> ApplyNextRequest();
  [[nodiscard]] core::Result<TransitionRecord> RequestFunctionGroupState(
    StateRequest request);
  [[nodiscard]] core::Result<FunctionGroupState> CurrentState(
    std::string_view function_group) const;
  [[nodiscard]] std::vector<StateRequest> PendingRequests() const;
  [[nodiscard]] const std::vector<TransitionRecord>& History() const noexcept {
    return history_;
  }
  [[nodiscard]] core::Result<bool> RegisterObserver(TransitionObserver observer);

private:
  struct FunctionGroupRecord final {
    FunctionGroupDefinition definition;
    FunctionGroupState state{FunctionGroupState::kOff};
  };

  struct PendingRequest final {
    std::uint64_t sequence{0U};
    StateRequest request;
  };

  [[nodiscard]] core::Result<FunctionGroupRecord*> FindFunctionGroup(
    std::string_view function_group);
  [[nodiscard]] core::Result<const FunctionGroupRecord*> FindFunctionGroup(
    std::string_view function_group) const;
  [[nodiscard]] core::Result<TransitionRecord> ApplyRequest(
    StateRequest request,
    std::uint64_t sequence);
  [[nodiscard]] core::Result<ProcessActionReport> ApplyProcessAction(
    const ProcessTransitionAction& action);
  [[nodiscard]] core::Result<bool> EnterFailureState(
    FunctionGroupRecord& function_group,
    const FunctionGroupTransition& transition,
    TransitionRecord& record);
  [[nodiscard]] core::Result<bool> ValidateDefinition(
    const FunctionGroupDefinition& definition) const;
  [[nodiscard]] core::Result<bool> ValidateRequest(const StateRequest& request) const;
  void StoreRecord(TransitionRecord record);
  void NotifyObservers(const TransitionRecord& record) const;

  ExecutionManager& execution_;
  std::vector<FunctionGroupRecord> function_groups_;
  std::vector<PendingRequest> pending_requests_;
  std::vector<TransitionRecord> history_;
  std::vector<TransitionObserver> observers_;
  std::uint64_t next_sequence_{1U};
};

[[nodiscard]] std::string_view ToString(StateRequestPriority priority) noexcept;
[[nodiscard]] std::string_view ToString(ProcessTransitionActionKind action) noexcept;

}  // namespace openautosar::runtime::state
