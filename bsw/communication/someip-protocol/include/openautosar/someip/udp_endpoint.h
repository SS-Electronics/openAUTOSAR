// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/someip/message.h"

#include <cstdint>
#include <string>
#include <sys/socket.h>

namespace openautosar::someip {

struct UdpEndpointAddress final {
  std::string host{"127.0.0.1"};
  std::uint16_t port{0U};

  friend bool operator==(const UdpEndpointAddress&, const UdpEndpointAddress&) = default;
};

struct UdpDatagram final {
  Message message{};
  UdpEndpointAddress remote{};
};

class UdpEndpoint final {
public:
  [[nodiscard]] static core::Result<UdpEndpoint> Bind(UdpEndpointAddress local);

  UdpEndpoint(const UdpEndpoint&) = delete;
  UdpEndpoint& operator=(const UdpEndpoint&) = delete;
  UdpEndpoint(UdpEndpoint&& other) noexcept;
  UdpEndpoint& operator=(UdpEndpoint&& other) noexcept;
  ~UdpEndpoint();

  [[nodiscard]] core::Result<UdpEndpointAddress> LocalAddress() const;
  [[nodiscard]] core::Result<std::size_t> SendTo(const Message& message, UdpEndpointAddress remote) const;
  [[nodiscard]] core::Result<UdpDatagram> Receive() const;

private:
  explicit UdpEndpoint(int fd) noexcept;

  void Close() noexcept;

  int fd_{-1};
};

}  // namespace openautosar::someip
