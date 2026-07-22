// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <cstddef>
#include <linux/can.h>
#include <string>

namespace openautosar::virtual_vehicle {

class SocketCanEndpoint final {
public:
  [[nodiscard]] static core::Result<SocketCanEndpoint> Open(std::string interface_name);

  SocketCanEndpoint(const SocketCanEndpoint&) = delete;
  SocketCanEndpoint& operator=(const SocketCanEndpoint&) = delete;
  SocketCanEndpoint(SocketCanEndpoint&& other) noexcept;
  SocketCanEndpoint& operator=(SocketCanEndpoint&& other) noexcept;
  ~SocketCanEndpoint();

  [[nodiscard]] core::Result<std::size_t> Send(const canfd_frame& frame) const;
  [[nodiscard]] core::Result<canfd_frame> Receive() const;

private:
  explicit SocketCanEndpoint(int fd) noexcept;

  void Close() noexcept;

  int fd_{-1};
};

}  // namespace openautosar::virtual_vehicle
