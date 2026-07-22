// SPDX-License-Identifier: MIT

#include "openautosar/someip/request_correlation.h"

#include <utility>

namespace openautosar::someip {

RequestCorrelator::RequestCorrelator(std::size_t capacity) : capacity_(capacity) {}

core::Result<RequestId> RequestCorrelator::Allocate(
  std::uint16_t client_id,
  MessageId message_id,
  std::uint64_t now_ns,
  std::uint64_t timeout_ns) {
  if (capacity_ == 0U || pending_.size() >= capacity_) {
    return core::Result<RequestId>::FromError({"someip-correlation", "correlation table is full"});
  }

  if (client_id == 0U) {
    return core::Result<RequestId>::FromError({"someip-correlation", "client id is zero"});
  }

  if (message_id.service_id == 0U) {
    return core::Result<RequestId>::FromError({"someip-correlation", "service id is zero"});
  }

  if (timeout_ns == 0U) {
    return core::Result<RequestId>::FromError({"someip-correlation", "timeout is zero"});
  }

  RequestId request_id{.client_id = client_id, .session_id = next_session_id_++};
  if (next_session_id_ == 0U) {
    next_session_id_ = 1U;
  }

  const PendingRequest pending{
    .request_id = request_id,
    .message_id = message_id,
    .deadline_ns = now_ns + timeout_ns,
  };
  pending_[KeyFor(request_id)] = pending;
  return core::Result<RequestId>::FromValue(request_id);
}

core::Result<PendingRequest> RequestCorrelator::Complete(
  RequestId request_id,
  MessageId message_id,
  std::uint64_t now_ns) {
  const auto key = KeyFor(request_id);
  const auto iter = pending_.find(key);
  if (iter == pending_.end()) {
    return core::Result<PendingRequest>::FromError(
      {"someip-correlation", "request id is not pending"});
  }

  if (iter->second.message_id != message_id) {
    return core::Result<PendingRequest>::FromError(
      {"someip-correlation", "message id does not match pending request"});
  }

  if (now_ns > iter->second.deadline_ns) {
    pending_.erase(iter);
    return core::Result<PendingRequest>::FromError({"someip-correlation", "request expired"});
  }

  auto completed = iter->second;
  pending_.erase(iter);
  return core::Result<PendingRequest>::FromValue(std::move(completed));
}

void RequestCorrelator::Expire(std::uint64_t now_ns) {
  for (auto iter = pending_.begin(); iter != pending_.end();) {
    if (now_ns > iter->second.deadline_ns) {
      iter = pending_.erase(iter);
    } else {
      ++iter;
    }
  }
}

std::uint32_t RequestCorrelator::KeyFor(RequestId request_id) noexcept {
  return static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(request_id.client_id) << 16U |
    static_cast<std::uint32_t>(request_id.session_id));
}

}  // namespace openautosar::someip
