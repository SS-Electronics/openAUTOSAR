// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/error_code.h"
#include "openautosar/core/result.h"

#include <memory>
#include <optional>
#include <utility>

namespace openautosar::core {
namespace detail {

template <typename T>
struct FutureSharedState final {
  std::optional<Result<T>> result;
};

}  // namespace detail

template <typename T>
class Promise;

template <typename T>
class Future final {
public:
  Future() : state_(std::make_shared<detail::FutureSharedState<T>>()) {}

  [[nodiscard]] bool IsReady() const noexcept {
    return state_ != nullptr && state_->result.has_value();
  }

  [[nodiscard]] Result<T> Get() const {
    if (state_ == nullptr) {
      return Result<T>::FromError({"core-future", "future state is not bound"});
    }

    if (!state_->result.has_value()) {
      return Result<T>::FromError({"core-future", "future is not ready"});
    }

    return state_->result.value();
  }

private:
  friend class Promise<T>;

  explicit Future(std::shared_ptr<detail::FutureSharedState<T>> state)
    : state_(std::move(state)) {}

  std::shared_ptr<detail::FutureSharedState<T>> state_;
};

template <typename T>
class Promise final {
public:
  Promise() : state_(std::make_shared<detail::FutureSharedState<T>>()) {}

  [[nodiscard]] Future<T> GetFuture() const { return Future<T>(state_); }

  [[nodiscard]] Result<bool> SetValue(T value) {
    if (state_->result.has_value()) {
      return Result<bool>::FromError({"core-future", "promise is already satisfied"});
    }

    state_->result = Result<T>::FromValue(std::move(value));
    return Result<bool>::FromValue(true);
  }

  [[nodiscard]] Result<bool> SetError(ErrorCode error) {
    if (state_->result.has_value()) {
      return Result<bool>::FromError({"core-future", "promise is already satisfied"});
    }

    state_->result = Result<T>::FromError(std::move(error));
    return Result<bool>::FromValue(true);
  }

private:
  std::shared_ptr<detail::FutureSharedState<T>> state_;
};

}  // namespace openautosar::core
