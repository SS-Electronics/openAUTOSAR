// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/someip/message.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace openautosar::someip {

struct PendingRequest final {
  RequestId request_id{};
  MessageId message_id{};
  std::uint64_t deadline_ns{0U};

  friend bool operator==(const PendingRequest&, const PendingRequest&) = default;
};

class RequestCorrelator final {
public:
  explicit RequestCorrelator(std::size_t capacity = 128U);

  [[nodiscard]] core::Result<RequestId> Allocate(
    std::uint16_t client_id,
    MessageId message_id,
    std::uint64_t now_ns,
    std::uint64_t timeout_ns);

  [[nodiscard]] core::Result<PendingRequest> Complete(
    RequestId request_id,
    MessageId message_id,
    std::uint64_t now_ns);

  void Expire(std::uint64_t now_ns);

  [[nodiscard]] std::size_t PendingCount() const noexcept { return pending_.size(); }

private:
  [[nodiscard]] static std::uint32_t KeyFor(RequestId request_id) noexcept;

  std::size_t capacity_{128U};
  std::uint16_t next_session_id_{1U};
  std::map<std::uint32_t, PendingRequest> pending_;
};

}  // namespace openautosar::someip
