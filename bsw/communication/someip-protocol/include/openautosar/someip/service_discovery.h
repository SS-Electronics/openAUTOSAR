// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/someip/message.h"

#include <cstdint>
#include <vector>

namespace openautosar::someip::sd {

inline constexpr MessageId kServiceDiscoveryMessageId{
  .service_id = 0xFFFFU,
  .method_id = 0x8100U,
};
inline constexpr std::uint8_t kServiceDiscoveryInterfaceVersion{1U};

enum class EntryType : std::uint8_t {
  kFindService = 0x00U,
  kOfferService = 0x01U,
  kSubscribeEventgroup = 0x06U,
  kSubscribeEventgroupAck = 0x07U,
};

struct ServiceEntry final {
  EntryType type{EntryType::kFindService};
  std::uint16_t service_id{0U};
  std::uint16_t instance_id{0U};
  std::uint8_t major_version{1U};
  std::uint32_t ttl{0U};
  std::uint32_t minor_version{0U};
  std::uint16_t eventgroup_id{0U};

  friend bool operator==(const ServiceEntry&, const ServiceEntry&) = default;
};

struct ServiceDiscoveryMessage final {
  std::uint32_t reboot_session{0U};
  std::vector<ServiceEntry> entries;

  friend bool operator==(const ServiceDiscoveryMessage&, const ServiceDiscoveryMessage&) = default;
};

[[nodiscard]] core::Result<std::vector<std::uint8_t>> SerializeServiceDiscoveryPayload(
  const ServiceDiscoveryMessage& message);
[[nodiscard]] core::Result<ServiceDiscoveryMessage> DeserializeServiceDiscoveryPayload(
  std::span<const std::uint8_t> payload);

[[nodiscard]] core::Result<Message> BuildServiceDiscoveryMessage(
  ServiceDiscoveryMessage discovery,
  RequestId request_id);
[[nodiscard]] core::Result<ServiceDiscoveryMessage> ExtractServiceDiscoveryMessage(
  const Message& message);

[[nodiscard]] const char* ToString(EntryType type) noexcept;

}  // namespace openautosar::someip::sd
