// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <string>
#include <string_view>

namespace openautosar::core {

class InstanceSpecifier final {
public:
  [[nodiscard]] static Result<InstanceSpecifier> Create(std::string value);

  [[nodiscard]] std::string_view Value() const noexcept { return value_; }

private:
  explicit InstanceSpecifier(std::string value);

  std::string value_;
};

}  // namespace openautosar::core
