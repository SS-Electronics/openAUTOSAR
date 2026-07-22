// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/error_code.h"

#include <utility>
#include <variant>

namespace openautosar::core {

template <typename T>
class Result final {
public:
  [[nodiscard]] static Result FromValue(T value) { return Result(std::move(value)); }

  [[nodiscard]] static Result FromError(ErrorCode error) { return Result(std::move(error)); }

  [[nodiscard]] bool HasValue() const noexcept { return std::holds_alternative<T>(storage_); }

  [[nodiscard]] explicit operator bool() const noexcept { return HasValue(); }

  [[nodiscard]] const T& Value() const { return std::get<T>(storage_); }

  [[nodiscard]] T& Value() { return std::get<T>(storage_); }

  [[nodiscard]] const ErrorCode& Error() const { return std::get<ErrorCode>(storage_); }

private:
  explicit Result(T value) : storage_(std::move(value)) {}

  explicit Result(ErrorCode error) : storage_(std::move(error)) {}

  std::variant<T, ErrorCode> storage_;
};

}  // namespace openautosar::core
