// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace openautosar::core {

struct ErrorCode final {
  std::string domain;
  std::string message;

  friend bool operator==(const ErrorCode&, const ErrorCode&) = default;
};

}  // namespace openautosar::core
