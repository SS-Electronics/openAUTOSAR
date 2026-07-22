// SPDX-License-Identifier: MIT

#include "openautosar/platform/bootstrap.h"
#include "openautosar/runtime/execution_manager.h"

#include <iostream>

int main() {
  openautosar::log::Logger logger("platform.bootstrap");
  openautosar::platform::PlatformBootstrap bootstrap(logger);

  const auto report = bootstrap.Run();
  if (!report) {
    std::cerr << "bootstrap failed: " << report.Error().domain << ": " << report.Error().message
              << '\n';
    return 1;
  }

  std::cout << "openAUTOSAR bootstrap state="
            << openautosar::runtime::ToString(report.Value().function_group)
            << " processes=" << report.Value().started_processes.size() << '\n';
  return 0;
}
