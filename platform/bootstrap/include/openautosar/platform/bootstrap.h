// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/log/logger.h"
#include "openautosar/runtime/execution_manager.h"
#include "openautosar/runtime/state_manager.h"

#include <string>
#include <vector>

namespace openautosar::platform {

struct BootstrapReport final {
  runtime::FunctionGroupState function_group{runtime::FunctionGroupState::kOff};
  std::vector<std::string> started_processes;
};

class PlatformBootstrap final {
public:
  explicit PlatformBootstrap(log::Logger& logger);

  [[nodiscard]] core::Result<BootstrapReport> Run();

private:
  log::Logger& logger_;
  runtime::ExecutionManager execution_;
  runtime::state::StateManager state_;
};

}  // namespace openautosar::platform
