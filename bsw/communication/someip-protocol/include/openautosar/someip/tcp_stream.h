// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/someip/message.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace openautosar::someip {

struct TcpEndpointAddress final {
  std::string host{"127.0.0.1"};
  std::uint16_t port{0U};

  friend bool operator==(const TcpEndpointAddress&, const TcpEndpointAddress&) = default;
};

class TcpConnection final {
public:
  [[nodiscard]] static core::Result<TcpConnection> Connect(TcpEndpointAddress remote);

  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;
  TcpConnection(TcpConnection&& other) noexcept;
  TcpConnection& operator=(TcpConnection&& other) noexcept;
  ~TcpConnection();

  [[nodiscard]] core::Result<std::size_t> Send(const Message& message) const;
  [[nodiscard]] core::Result<Message> Receive() const;

private:
  friend class TcpServer;

  explicit TcpConnection(int fd) noexcept;

  void Close() noexcept;

  int fd_{-1};
};

class TcpServer final {
public:
  [[nodiscard]] static core::Result<TcpServer> Listen(
    TcpEndpointAddress local,
    std::size_t backlog = 1U);

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;
  TcpServer(TcpServer&& other) noexcept;
  TcpServer& operator=(TcpServer&& other) noexcept;
  ~TcpServer();

  [[nodiscard]] core::Result<TcpEndpointAddress> LocalAddress() const;
  [[nodiscard]] core::Result<TcpConnection> Accept() const;

private:
  explicit TcpServer(int fd) noexcept;

  void Close() noexcept;

  int fd_{-1};
};

}  // namespace openautosar::someip
