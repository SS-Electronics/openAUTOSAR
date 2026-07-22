// SPDX-License-Identifier: MIT

#include "openautosar/dds/rtps/udp_endpoint.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace openautosar::dds::rtps {
namespace {

[[nodiscard]] core::ErrorCode MakeErrnoError(std::string message) {
  return {"dds-rtps-udp", std::move(message) + ": " + std::strerror(errno)};
}

[[nodiscard]] core::Result<sockaddr_in> ToSockaddr(const UdpEndpointAddress& address) {
  sockaddr_in socket_address{};
  socket_address.sin_family = AF_INET;
  socket_address.sin_port = htons(address.port);

  if (::inet_pton(AF_INET, address.host.c_str(), &socket_address.sin_addr) != 1) {
    return core::Result<sockaddr_in>::FromError({"dds-rtps-udp", "invalid IPv4 address"});
  }

  return core::Result<sockaddr_in>::FromValue(socket_address);
}

[[nodiscard]] UdpEndpointAddress FromSockaddr(const sockaddr_in& address) {
  char buffer[INET_ADDRSTRLEN]{};
  const auto* text = ::inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
  return {
    .host = text == nullptr ? std::string{} : std::string{text},
    .port = ntohs(address.sin_port),
  };
}

}  // namespace

core::Result<UdpEndpoint> UdpEndpoint::Bind(UdpEndpointAddress local) {
  auto address = ToSockaddr(local);
  if (!address) {
    return core::Result<UdpEndpoint>::FromError(address.Error());
  }

  const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    return core::Result<UdpEndpoint>::FromError(MakeErrnoError("failed to create UDP socket"));
  }

  int reuse = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    const auto error = MakeErrnoError("failed to set SO_REUSEADDR");
    ::close(fd);
    return core::Result<UdpEndpoint>::FromError(error);
  }

  if (::bind(fd, reinterpret_cast<const sockaddr*>(&address.Value()), sizeof(sockaddr_in)) < 0) {
    const auto error = MakeErrnoError("failed to bind UDP socket");
    ::close(fd);
    return core::Result<UdpEndpoint>::FromError(error);
  }

  return core::Result<UdpEndpoint>::FromValue(UdpEndpoint(fd));
}

UdpEndpoint::UdpEndpoint(UdpEndpoint&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

UdpEndpoint& UdpEndpoint::operator=(UdpEndpoint&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  Close();
  fd_ = other.fd_;
  other.fd_ = -1;
  return *this;
}

UdpEndpoint::~UdpEndpoint() { Close(); }

core::Result<UdpEndpointAddress> UdpEndpoint::LocalAddress() const {
  if (fd_ < 0) {
    return core::Result<UdpEndpointAddress>::FromError({"dds-rtps-udp", "endpoint is closed"});
  }

  sockaddr_in address{};
  socklen_t length = sizeof(address);
  if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
    return core::Result<UdpEndpointAddress>::FromError(
      MakeErrnoError("failed to read local address"));
  }

  return core::Result<UdpEndpointAddress>::FromValue(FromSockaddr(address));
}

core::Result<std::size_t> UdpEndpoint::SendTo(
  const RtpsMessage& message,
  UdpEndpointAddress remote) const {
  if (fd_ < 0) {
    return core::Result<std::size_t>::FromError({"dds-rtps-udp", "endpoint is closed"});
  }

  auto bytes = SerializeRtpsMessage(message);
  if (!bytes) {
    return core::Result<std::size_t>::FromError(bytes.Error());
  }

  auto remote_address = ToSockaddr(remote);
  if (!remote_address) {
    return core::Result<std::size_t>::FromError(remote_address.Error());
  }

  const auto sent = ::sendto(
    fd_,
    bytes.Value().data(),
    bytes.Value().size(),
    0,
    reinterpret_cast<const sockaddr*>(&remote_address.Value()),
    sizeof(sockaddr_in));
  if (sent < 0) {
    return core::Result<std::size_t>::FromError(MakeErrnoError("failed to send UDP datagram"));
  }

  return core::Result<std::size_t>::FromValue(static_cast<std::size_t>(sent));
}

core::Result<UdpDatagram> UdpEndpoint::Receive() const {
  if (fd_ < 0) {
    return core::Result<UdpDatagram>::FromError({"dds-rtps-udp", "endpoint is closed"});
  }

  std::vector<std::uint8_t> bytes(kMaxUdpPayloadSize);
  sockaddr_in remote{};
  socklen_t remote_length = sizeof(remote);
  const auto received = ::recvfrom(
    fd_,
    bytes.data(),
    bytes.size(),
    0,
    reinterpret_cast<sockaddr*>(&remote),
    &remote_length);
  if (received < 0) {
    return core::Result<UdpDatagram>::FromError(MakeErrnoError("failed to receive UDP datagram"));
  }

  bytes.resize(static_cast<std::size_t>(received));
  auto message = DeserializeRtpsMessage(bytes);
  if (!message) {
    return core::Result<UdpDatagram>::FromError(message.Error());
  }

  return core::Result<UdpDatagram>::FromValue({
    .message = std::move(message.Value()),
    .remote = FromSockaddr(remote),
  });
}

UdpEndpoint::UdpEndpoint(int fd) noexcept : fd_(fd) {}

void UdpEndpoint::Close() noexcept {
  if (fd_ >= 0) {
    static_cast<void>(::close(fd_));
    fd_ = -1;
  }
}

}  // namespace openautosar::dds::rtps
