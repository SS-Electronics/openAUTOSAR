// SPDX-License-Identifier: MIT

#include "openautosar/someip/tcp_stream.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <netinet/in.h>
#include <span>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace openautosar::someip {
namespace {

[[nodiscard]] core::ErrorCode MakeErrnoError(std::string message) {
  return {"someip-tcp", std::move(message) + ": " + std::strerror(errno)};
}

[[nodiscard]] core::Result<sockaddr_in> ToSockaddr(const TcpEndpointAddress& address) {
  sockaddr_in socket_address{};
  socket_address.sin_family = AF_INET;
  socket_address.sin_port = htons(address.port);

  if (::inet_pton(AF_INET, address.host.c_str(), &socket_address.sin_addr) != 1) {
    return core::Result<sockaddr_in>::FromError({"someip-tcp", "invalid IPv4 address"});
  }

  return core::Result<sockaddr_in>::FromValue(socket_address);
}

[[nodiscard]] TcpEndpointAddress FromSockaddr(const sockaddr_in& address) {
  char buffer[INET_ADDRSTRLEN]{};
  const auto* text = ::inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
  return {
    .host = text == nullptr ? std::string{} : std::string{text},
    .port = ntohs(address.sin_port),
  };
}

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(bytes[offset]) << 24U |
    static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U |
    static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U |
    static_cast<std::uint32_t>(bytes[offset + 3U]));
}

[[nodiscard]] core::Result<bool> ReadExact(
  int fd,
  std::span<std::uint8_t> destination,
  const char* operation) {
  std::size_t offset{0U};
  while (offset < destination.size()) {
    const auto received = ::recv(
      fd,
      destination.data() + static_cast<std::ptrdiff_t>(offset),
      destination.size() - offset,
      0);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      return core::Result<bool>::FromError(MakeErrnoError(operation));
    }

    if (received == 0) {
      return core::Result<bool>::FromError(
        {"someip-tcp", "TCP peer closed while receiving SOME/IP frame"});
    }

    offset += static_cast<std::size_t>(received);
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<std::size_t> WriteAll(int fd, std::span<const std::uint8_t> source) {
  std::size_t offset{0U};
  while (offset < source.size()) {
    const auto sent = ::send(
      fd,
      source.data() + static_cast<std::ptrdiff_t>(offset),
      source.size() - offset,
      MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno == EINTR) {
        continue;
      }
      return core::Result<std::size_t>::FromError(
        MakeErrnoError("failed to send TCP SOME/IP frame"));
    }

    if (sent == 0) {
      return core::Result<std::size_t>::FromError(
        {"someip-tcp", "TCP peer closed while sending SOME/IP frame"});
    }

    offset += static_cast<std::size_t>(sent);
  }

  return core::Result<std::size_t>::FromValue(source.size());
}

}  // namespace

core::Result<TcpConnection> TcpConnection::Connect(TcpEndpointAddress remote) {
  auto address = ToSockaddr(remote);
  if (!address) {
    return core::Result<TcpConnection>::FromError(address.Error());
  }

  const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    return core::Result<TcpConnection>::FromError(MakeErrnoError("failed to create TCP socket"));
  }

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&address.Value()), sizeof(sockaddr_in)) < 0) {
    const auto error = MakeErrnoError("failed to connect TCP socket");
    static_cast<void>(::close(fd));
    return core::Result<TcpConnection>::FromError(error);
  }

  return core::Result<TcpConnection>::FromValue(TcpConnection(fd));
}

TcpConnection::TcpConnection(TcpConnection&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  Close();
  fd_ = other.fd_;
  other.fd_ = -1;
  return *this;
}

TcpConnection::~TcpConnection() { Close(); }

core::Result<std::size_t> TcpConnection::Send(const Message& message) const {
  if (fd_ < 0) {
    return core::Result<std::size_t>::FromError({"someip-tcp", "connection is closed"});
  }

  auto bytes = SerializeMessage(message);
  if (!bytes) {
    return core::Result<std::size_t>::FromError(bytes.Error());
  }

  return WriteAll(fd_, bytes.Value());
}

core::Result<Message> TcpConnection::Receive() const {
  if (fd_ < 0) {
    return core::Result<Message>::FromError({"someip-tcp", "connection is closed"});
  }

  std::vector<std::uint8_t> bytes(kHeaderSize);
  auto header = ReadExact(fd_, bytes, "failed to receive TCP SOME/IP header");
  if (!header) {
    return core::Result<Message>::FromError(header.Error());
  }

  const auto length = ReadU32(bytes, 4U);
  if (length < kSomeIpLengthBase) {
    return core::Result<Message>::FromError({"someip-tcp", "invalid SOME/IP length field"});
  }

  const auto payload_size = static_cast<std::size_t>(length) - kSomeIpLengthBase;
  if (payload_size > kMaxUdpPayloadSize - kHeaderSize) {
    return core::Result<Message>::FromError(
      {"someip-tcp", "TCP SOME/IP frame exceeds configured payload limit"});
  }

  bytes.resize(kHeaderSize + payload_size);
  auto payload = ReadExact(
    fd_,
    std::span<std::uint8_t>{bytes}.subspan(kHeaderSize),
    "failed to receive TCP SOME/IP payload");
  if (!payload) {
    return core::Result<Message>::FromError(payload.Error());
  }

  return DeserializeMessage(bytes);
}

TcpConnection::TcpConnection(int fd) noexcept : fd_(fd) {}

void TcpConnection::Close() noexcept {
  if (fd_ >= 0) {
    static_cast<void>(::close(fd_));
    fd_ = -1;
  }
}

core::Result<TcpServer> TcpServer::Listen(TcpEndpointAddress local, std::size_t backlog) {
  auto address = ToSockaddr(local);
  if (!address) {
    return core::Result<TcpServer>::FromError(address.Error());
  }

  if (backlog == 0U || backlog > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return core::Result<TcpServer>::FromError({"someip-tcp", "TCP listen backlog is invalid"});
  }

  const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    return core::Result<TcpServer>::FromError(MakeErrnoError("failed to create TCP listen socket"));
  }

  int reuse = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    const auto error = MakeErrnoError("failed to set SO_REUSEADDR");
    static_cast<void>(::close(fd));
    return core::Result<TcpServer>::FromError(error);
  }

  if (::bind(fd, reinterpret_cast<const sockaddr*>(&address.Value()), sizeof(sockaddr_in)) < 0) {
    const auto error = MakeErrnoError("failed to bind TCP listen socket");
    static_cast<void>(::close(fd));
    return core::Result<TcpServer>::FromError(error);
  }

  if (::listen(fd, static_cast<int>(backlog)) < 0) {
    const auto error = MakeErrnoError("failed to listen on TCP socket");
    static_cast<void>(::close(fd));
    return core::Result<TcpServer>::FromError(error);
  }

  return core::Result<TcpServer>::FromValue(TcpServer(fd));
}

TcpServer::TcpServer(TcpServer&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

TcpServer& TcpServer::operator=(TcpServer&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  Close();
  fd_ = other.fd_;
  other.fd_ = -1;
  return *this;
}

TcpServer::~TcpServer() { Close(); }

core::Result<TcpEndpointAddress> TcpServer::LocalAddress() const {
  if (fd_ < 0) {
    return core::Result<TcpEndpointAddress>::FromError({"someip-tcp", "server is closed"});
  }

  sockaddr_in address{};
  socklen_t length = sizeof(address);
  if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
    return core::Result<TcpEndpointAddress>::FromError(
      MakeErrnoError("failed to read local address"));
  }

  return core::Result<TcpEndpointAddress>::FromValue(FromSockaddr(address));
}

core::Result<TcpConnection> TcpServer::Accept() const {
  if (fd_ < 0) {
    return core::Result<TcpConnection>::FromError({"someip-tcp", "server is closed"});
  }

  sockaddr_in remote{};
  socklen_t remote_length = sizeof(remote);
  const int fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&remote), &remote_length);
  if (fd < 0) {
    return core::Result<TcpConnection>::FromError(MakeErrnoError("failed to accept TCP socket"));
  }

  return core::Result<TcpConnection>::FromValue(TcpConnection(fd));
}

TcpServer::TcpServer(int fd) noexcept : fd_(fd) {}

void TcpServer::Close() noexcept {
  if (fd_ >= 0) {
    static_cast<void>(::close(fd_));
    fd_ = -1;
  }
}

}  // namespace openautosar::someip
