// SPDX-License-Identifier: MIT

#include "openautosar/core/instance_specifier.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace openautosar::core {
namespace {

[[nodiscard]] bool IsAllowedCharacter(const char value) noexcept {
  const auto byte = static_cast<unsigned char>(value);
  return std::isalnum(byte) != 0 || value == '_' || value == '-' || value == '/' || value == '.';
}

}  // namespace

InstanceSpecifier::InstanceSpecifier(std::string value) : value_(std::move(value)) {}

Result<InstanceSpecifier> InstanceSpecifier::Create(std::string value) {
  if (value.empty()) {
    return Result<InstanceSpecifier>::FromError({"ara-core", "instance specifier is empty"});
  }

  if (value.front() != '/') {
    return Result<InstanceSpecifier>::FromError({"ara-core", "instance specifier must start with /"});
  }

  if (value.find("//") != std::string::npos) {
    return Result<InstanceSpecifier>::FromError({"ara-core", "instance specifier contains //"});
  }

  const auto valid = std::all_of(value.begin(), value.end(), IsAllowedCharacter);
  if (!valid) {
    return Result<InstanceSpecifier>::FromError(
      {"ara-core", "instance specifier contains unsupported characters"});
  }

  return Result<InstanceSpecifier>::FromValue(InstanceSpecifier(std::move(value)));
}

}  // namespace openautosar::core
