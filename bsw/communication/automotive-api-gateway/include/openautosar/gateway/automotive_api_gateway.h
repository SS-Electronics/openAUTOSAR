// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/com/service_registry.h"
#include "openautosar/core/result.h"
#include "openautosar/security/identity_access_manager.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::gateway {

enum class GatewayProtocol {
  kRestJson,
  kGrpc,
  kWebSocket,
  kMqtt,
  kTestLoopback,
};

enum class GatewayOperation {
  kDiscoverService,
  kPublishEvent,
};

enum class GatewayReceiptStatus {
  kAccepted,
  kRouteRejected,
  kUnauthorized,
  kRateLimited,
  kPayloadRejected,
  kServiceUnavailable,
};

struct GatewayPolicy final {
  std::size_t max_routes{32U};
  std::size_t max_payload_bytes{1024U};
  std::size_t max_receipts{64U};
  bool require_authenticated_clients{true};
  bool reject_remote_clients_by_default{true};
};

struct ApiRoute final {
  std::string route_id;
  std::string path;
  GatewayProtocol protocol{GatewayProtocol::kRestJson};
  GatewayOperation operation{GatewayOperation::kDiscoverService};
  com::ServiceIdentifier service{};
  std::string event_name;
  std::string access_policy;
  std::string deployment_ref;
  bool enabled{true};
  bool allow_remote{false};
  bool require_authenticated{true};
  std::size_t max_payload_bytes{256U};
  std::uint32_t max_requests_per_window{20U};
  std::uint64_t rate_window_ms{1'000U};
};

struct ClientContext final {
  std::string client_id;
  std::string machine_id;
  std::string security_label;
  std::vector<std::string> roles;
  bool authenticated{false};
  bool remote{true};
};

struct ApiRequest final {
  std::string route_id;
  std::vector<std::uint8_t> payload;
  std::uint64_t monotonic_ms{0U};
  std::string correlation_id;
};

struct ApiReceipt final {
  GatewayReceiptStatus status{GatewayReceiptStatus::kRouteRejected};
  std::string route_id;
  std::string client_id;
  com::ServiceIdentifier service{};
  std::uint64_t gateway_sequence{0U};
  std::uint64_t service_sequence{0U};
  std::size_t payload_size{0U};
  std::string reason;
  std::string audit_label;
  bool accepted{false};
};

struct GatewaySnapshot final {
  std::vector<ApiRoute> routes;
  std::uint64_t accepted_requests{0U};
  std::uint64_t rejected_requests{0U};
  std::uint64_t unauthorized_requests{0U};
  std::uint64_t rate_limited_requests{0U};
  std::uint64_t payload_rejected_requests{0U};
  std::uint64_t service_unavailable_requests{0U};
  std::vector<ApiReceipt> receipts;
};

class AutomotiveApiGateway final {
public:
  explicit AutomotiveApiGateway(
    com::ServiceRegistry& registry,
    GatewayPolicy policy = {});

  void SetAccessPolicy(const security::AccessPolicyEngine& access_policy) noexcept;
  void ClearAccessPolicy() noexcept;
  void SetSecurityEventCollector(security::SecurityEventCollector& collector) noexcept;
  void ClearSecurityEventCollector() noexcept;

  [[nodiscard]] core::Result<ApiRoute> RegisterRoute(ApiRoute route);
  [[nodiscard]] core::Result<bool> RemoveRoute(std::string_view route_id);
  [[nodiscard]] core::Result<ApiReceipt> Dispatch(
    const ClientContext& client,
    ApiRequest request);
  [[nodiscard]] std::vector<ApiRoute> Routes() const { return routes_; }
  [[nodiscard]] GatewaySnapshot Snapshot() const;

private:
  struct ClientWindow final {
    std::string client_id;
    std::string route_id;
    std::uint64_t window_start_ms{0U};
    std::uint32_t count{0U};
  };

  [[nodiscard]] core::Result<bool> ValidateRoute(const ApiRoute& route) const;
  [[nodiscard]] std::optional<std::size_t> FindRouteIndex(std::string_view route_id) const;
  [[nodiscard]] std::optional<std::size_t> FindPathIndex(std::string_view path) const;
  [[nodiscard]] core::Result<bool> Authorize(
    const ClientContext& client,
    const ApiRoute& route,
    std::uint64_t timestamp_ms) const;
  [[nodiscard]] bool IsRateLimited(
    const ClientContext& client,
    const ApiRoute& route,
    std::uint64_t timestamp_ms);
  [[nodiscard]] ApiReceipt MakeReceipt(
    const ApiRoute* route,
    const ClientContext& client,
    const ApiRequest& request,
    GatewayReceiptStatus status,
    std::string reason) const;
  [[nodiscard]] core::Result<ApiReceipt> Accept(ApiReceipt receipt);
  [[nodiscard]] core::Result<ApiReceipt> Block(ApiReceipt receipt);
  void RecordReceipt(ApiReceipt receipt);
  void CountBlocked(GatewayReceiptStatus status);
  void RecordAuthorizationFailure(
    const ClientContext& client,
    const ApiRoute& route,
    std::string_view reason,
    std::uint64_t timestamp_ms) const;

  com::ServiceRegistry* registry_{nullptr};
  GatewayPolicy policy_{};
  std::vector<ApiRoute> routes_;
  std::vector<ClientWindow> windows_;
  std::vector<ApiReceipt> receipts_;
  const security::AccessPolicyEngine* access_policy_{nullptr};
  security::SecurityEventCollector* security_events_{nullptr};
  std::uint64_t next_sequence_{1U};
  std::uint64_t accepted_requests_{0U};
  std::uint64_t rejected_requests_{0U};
  std::uint64_t unauthorized_requests_{0U};
  std::uint64_t rate_limited_requests_{0U};
  std::uint64_t payload_rejected_requests_{0U};
  std::uint64_t service_unavailable_requests_{0U};
};

[[nodiscard]] std::string_view ToString(GatewayProtocol protocol) noexcept;
[[nodiscard]] std::string_view ToString(GatewayOperation operation) noexcept;
[[nodiscard]] std::string_view ToString(GatewayReceiptStatus status) noexcept;

}  // namespace openautosar::gateway
