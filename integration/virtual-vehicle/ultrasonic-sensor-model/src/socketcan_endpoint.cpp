// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/socketcan_endpoint.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <linux/can/raw.h>
#include <net/if.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace openautosar::virtual_vehicle {
namespace {

[[nodiscard]] core::ErrorCode MakeErrnoError(std::string message) {
  return {"socketcan", std::move(message) + ": " + std::strerror(errno)};
}

}  // namespace

core::Result<SocketCanEndpoint> SocketCanEndpoint::Open(std::string interface_name) {
  if (interface_name.empty()) {
    return core::Result<SocketCanEndpoint>::FromError({"socketcan", "interface name is empty"});
  }

  const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    return core::Result<SocketCanEndpoint>::FromError(MakeErrnoError("failed to create CAN socket"));
  }

  int enable_fd_frames = 1;
  if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd_frames, sizeof(enable_fd_frames)) <
      0) {
    const auto error = MakeErrnoError("failed to enable CAN FD frames");
    ::close(fd);
    return core::Result<SocketCanEndpoint>::FromError(error);
  }

  ifreq request{};
  const auto copy_size = std::min(interface_name.size(), sizeof(request.ifr_name) - 1U);
  std::memcpy(request.ifr_name, interface_name.data(), copy_size);

  if (::ioctl(fd, SIOCGIFINDEX, &request) < 0) {
    const auto error = MakeErrnoError("failed to resolve CAN interface");
    ::close(fd);
    return core::Result<SocketCanEndpoint>::FromError(error);
  }

  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = request.ifr_ifindex;

  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    const auto error = MakeErrnoError("failed to bind CAN socket");
    ::close(fd);
    return core::Result<SocketCanEndpoint>::FromError(error);
  }

  return core::Result<SocketCanEndpoint>::FromValue(SocketCanEndpoint(fd));
}

SocketCanEndpoint::SocketCanEndpoint(SocketCanEndpoint&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

SocketCanEndpoint& SocketCanEndpoint::operator=(SocketCanEndpoint&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  Close();
  fd_ = other.fd_;
  other.fd_ = -1;
  return *this;
}

SocketCanEndpoint::~SocketCanEndpoint() { Close(); }

core::Result<std::size_t> SocketCanEndpoint::Send(const canfd_frame& frame) const {
  if (fd_ < 0) {
    return core::Result<std::size_t>::FromError({"socketcan", "endpoint is closed"});
  }

  const auto written = ::write(fd_, &frame, CANFD_MTU);
  if (written < 0) {
    return core::Result<std::size_t>::FromError(MakeErrnoError("failed to send CAN frame"));
  }

  if (written != CANFD_MTU) {
    return core::Result<std::size_t>::FromError({"socketcan", "partial CAN frame write"});
  }

  return core::Result<std::size_t>::FromValue(static_cast<std::size_t>(written));
}

core::Result<canfd_frame> SocketCanEndpoint::Receive() const {
  if (fd_ < 0) {
    return core::Result<canfd_frame>::FromError({"socketcan", "endpoint is closed"});
  }

  canfd_frame frame{};
  const auto received = ::read(fd_, &frame, CANFD_MTU);
  if (received < 0) {
    return core::Result<canfd_frame>::FromError(MakeErrnoError("failed to receive CAN frame"));
  }

  if (received != CANFD_MTU && received != CAN_MTU) {
    return core::Result<canfd_frame>::FromError({"socketcan", "unexpected CAN frame size"});
  }

  return core::Result<canfd_frame>::FromValue(frame);
}

SocketCanEndpoint::SocketCanEndpoint(int fd) noexcept : fd_(fd) {}

void SocketCanEndpoint::Close() noexcept {
  if (fd_ >= 0) {
    static_cast<void>(::close(fd_));
    fd_ = -1;
  }
}

}  // namespace openautosar::virtual_vehicle
