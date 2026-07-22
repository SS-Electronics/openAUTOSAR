// SPDX-License-Identifier: MIT

#include "openautosar/gateway/automotive_api_gateway.h"

#include <algorithm>
#include <utility>

namespace openautosar::gateway {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"automotive-api-gateway", message};
}

[[nodiscard]] bool IsSafeText(std::string_view value) noexcept {
  if (value.empty() || value.size() > 160U) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](char item) {
    const auto byte = static_cast<unsigned char>(item);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || item == '-' || item == '_' ||
           item == '.' || item == ':' || item == '/';
  });
}

[[nodiscard]] bool IsValidService(const com::ServiceIdentifier& service) noexcept {
  return service.interface_id != 0U && service.instance_id != 0U &&
         service.major_version != 0U;
}

[[nodiscard]] std::string ClientId(const ClientContext& client) {
  if (!client.client_id.empty()) {
    return client.client_id;
  }

  return "anonymous";
}

[[nodiscard]] std::size_t EffectivePayloadLimit(
  const GatewayPolicy& policy,
  const ApiRoute& route) noexcept {
  return std::min(policy.max_payload_bytes, route.max_payload_bytes);
}

}  // namespace

AutomotiveApiGateway::AutomotiveApiGateway(
  com::ServiceRegistry& registry,
  GatewayPolicy policy)
  : registry_(&registry), policy_(policy) {}

void AutomotiveApiGateway::SetAccessPolicy(
  const security::AccessPolicyEngine& access_policy) noexcept {
  access_policy_ = &access_policy;
}

void AutomotiveApiGateway::ClearAccessPolicy() noexcept {
  access_policy_ = nullptr;
}

void AutomotiveApiGateway::SetSecurityEventCollector(
  security::SecurityEventCollector& collector) noexcept {
  security_events_ = &collector;
}

void AutomotiveApiGateway::ClearSecurityEventCollector() noexcept {
  security_events_ = nullptr;
}

core::Result<ApiRoute> AutomotiveApiGateway::RegisterRoute(ApiRoute route) {
  auto validation = ValidateRoute(route);
  if (!validation) {
    return core::Result<ApiRoute>::FromError(validation.Error());
  }

  if (routes_.size() >= policy_.max_routes) {
    return core::Result<ApiRoute>::FromError(MakeError("API gateway route budget exceeded"));
  }

  if (FindRouteIndex(route.route_id).has_value()) {
    return core::Result<ApiRoute>::FromError(MakeError("API gateway route id is duplicated"));
  }

  if (FindPathIndex(route.path).has_value()) {
    return core::Result<ApiRoute>::FromError(MakeError("API gateway path is duplicated"));
  }

  routes_.push_back(std::move(route));
  return core::Result<ApiRoute>::FromValue(routes_.back());
}

core::Result<bool> AutomotiveApiGateway::RemoveRoute(std::string_view route_id) {
  auto index = FindRouteIndex(route_id);
  if (!index.has_value()) {
    return core::Result<bool>::FromError(MakeError("API gateway route was not found"));
  }

  routes_.erase(routes_.begin() + static_cast<std::ptrdiff_t>(index.value()));
  return core::Result<bool>::FromValue(true);
}

core::Result<ApiReceipt> AutomotiveApiGateway::Dispatch(
  const ClientContext& client,
  ApiRequest request) {
  const auto index = FindRouteIndex(request.route_id);
  if (!index.has_value()) {
    return Block(MakeReceipt(
      nullptr,
      client,
      request,
      GatewayReceiptStatus::kRouteRejected,
      "API gateway route was not found"));
  }

  const auto& route = routes_[index.value()];
  if (!route.enabled) {
    return Block(MakeReceipt(
      &route,
      client,
      request,
      GatewayReceiptStatus::kRouteRejected,
      "API gateway route is disabled"));
  }

  auto authorized = Authorize(client, route, request.monotonic_ms);
  if (!authorized) {
    return Block(MakeReceipt(
      &route,
      client,
      request,
      GatewayReceiptStatus::kUnauthorized,
      authorized.Error().message));
  }

  const auto payload_limit = EffectivePayloadLimit(policy_, route);
  if (request.payload.size() > payload_limit) {
    return Block(MakeReceipt(
      &route,
      client,
      request,
      GatewayReceiptStatus::kPayloadRejected,
      "API gateway payload exceeds route limit"));
  }

  if (route.operation == GatewayOperation::kPublishEvent && request.payload.empty()) {
    return Block(MakeReceipt(
      &route,
      client,
      request,
      GatewayReceiptStatus::kPayloadRejected,
      "API gateway publish payload is empty"));
  }

  if (IsRateLimited(client, route, request.monotonic_ms)) {
    return Block(MakeReceipt(
      &route,
      client,
      request,
      GatewayReceiptStatus::kRateLimited,
      "API gateway route rate limit exceeded"));
  }

  if (route.operation == GatewayOperation::kDiscoverService) {
    if (registry_->FindService(route.service).empty()) {
      return Block(MakeReceipt(
        &route,
        client,
        request,
        GatewayReceiptStatus::kServiceUnavailable,
        "API gateway target service is unavailable"));
    }

    return Accept(MakeReceipt(
      &route,
      client,
      request,
      GatewayReceiptStatus::kAccepted,
      "API gateway service discovery accepted"));
  }

  auto published = registry_->Publish({
    .service = route.service,
    .event_name = route.event_name,
    .payload = std::move(request.payload),
  });
  if (!published) {
    return Block(MakeReceipt(
      &route,
      client,
      request,
      GatewayReceiptStatus::kServiceUnavailable,
      published.Error().message));
  }

  auto receipt = MakeReceipt(
    &route,
    client,
    request,
    GatewayReceiptStatus::kAccepted,
    "API gateway event publish accepted");
  receipt.service_sequence = published.Value();
  return Accept(std::move(receipt));
}

GatewaySnapshot AutomotiveApiGateway::Snapshot() const {
  return {
    .routes = routes_,
    .accepted_requests = accepted_requests_,
    .rejected_requests = rejected_requests_,
    .unauthorized_requests = unauthorized_requests_,
    .rate_limited_requests = rate_limited_requests_,
    .payload_rejected_requests = payload_rejected_requests_,
    .service_unavailable_requests = service_unavailable_requests_,
    .receipts = receipts_,
  };
}

core::Result<bool> AutomotiveApiGateway::ValidateRoute(const ApiRoute& route) const {
  if (policy_.max_routes == 0U || policy_.max_payload_bytes == 0U) {
    return core::Result<bool>::FromError(MakeError("API gateway policy is invalid"));
  }

  if (!IsSafeText(route.route_id) || !IsSafeText(route.path) || route.path.front() != '/') {
    return core::Result<bool>::FromError(MakeError("API gateway route identity is invalid"));
  }

  if (!IsValidService(route.service)) {
    return core::Result<bool>::FromError(MakeError("API gateway service id is invalid"));
  }

  if (route.deployment_ref.empty()) {
    return core::Result<bool>::FromError(MakeError("API gateway route provenance is missing"));
  }

  if (route.max_payload_bytes == 0U || route.rate_window_ms == 0U ||
      route.max_requests_per_window == 0U) {
    return core::Result<bool>::FromError(MakeError("API gateway route limit is invalid"));
  }

  if (route.operation == GatewayOperation::kPublishEvent && route.event_name.empty()) {
    return core::Result<bool>::FromError(MakeError("API gateway event name is empty"));
  }

  return core::Result<bool>::FromValue(true);
}

std::optional<std::size_t> AutomotiveApiGateway::FindRouteIndex(
  std::string_view route_id) const {
  const auto iter = std::find_if(routes_.begin(), routes_.end(), [route_id](const auto& route) {
    return route.route_id == route_id;
  });
  if (iter == routes_.end()) {
    return std::nullopt;
  }

  return static_cast<std::size_t>(std::distance(routes_.begin(), iter));
}

std::optional<std::size_t> AutomotiveApiGateway::FindPathIndex(std::string_view path) const {
  const auto iter = std::find_if(routes_.begin(), routes_.end(), [path](const auto& route) {
    return route.path == path;
  });
  if (iter == routes_.end()) {
    return std::nullopt;
  }

  return static_cast<std::size_t>(std::distance(routes_.begin(), iter));
}

core::Result<bool> AutomotiveApiGateway::Authorize(
  const ClientContext& client,
  const ApiRoute& route,
  std::uint64_t timestamp_ms) const {
  if ((policy_.require_authenticated_clients || route.require_authenticated) &&
      !client.authenticated) {
    RecordAuthorizationFailure(
      client,
      route,
      "unauthenticated API gateway client rejected",
      timestamp_ms);
    return core::Result<bool>::FromError(
      MakeError("unauthenticated API gateway client rejected"));
  }

  if (policy_.reject_remote_clients_by_default && client.remote && !route.allow_remote) {
    RecordAuthorizationFailure(
      client,
      route,
      "remote API gateway client rejected by route policy",
      timestamp_ms);
    return core::Result<bool>::FromError(
      MakeError("remote API gateway client rejected by route policy"));
  }

  if (access_policy_ == nullptr) {
    return core::Result<bool>::FromValue(true);
  }

  security::Principal principal;
  principal.application_id = ClientId(client);
  principal.machine_id = client.machine_id;
  principal.security_label = client.security_label;
  principal.roles = client.roles;
  principal.authenticated = client.authenticated;
  principal.remote = client.remote;

  const auto decision = access_policy_->Authorize({
    .principal = std::move(principal),
    .resource = {
      .kind = security::ResourceKind::kApiGateway,
      .identifier = route.route_id,
      .policy_id = route.access_policy,
    },
    .operation = security::Operation::kInvokeGatewayRoute,
    .action = std::string(ToString(route.operation)),
  });
  if (!decision.Allowed()) {
    RecordAuthorizationFailure(client, route, decision.reason, timestamp_ms);
    return core::Result<bool>::FromError({"automotive-api-gateway", decision.reason});
  }

  return core::Result<bool>::FromValue(true);
}

bool AutomotiveApiGateway::IsRateLimited(
  const ClientContext& client,
  const ApiRoute& route,
  std::uint64_t timestamp_ms) {
  auto iter = std::find_if(windows_.begin(), windows_.end(), [&client, &route](const auto& item) {
    return item.client_id == ClientId(client) && item.route_id == route.route_id;
  });

  if (iter == windows_.end()) {
    windows_.push_back({
      .client_id = ClientId(client),
      .route_id = route.route_id,
      .window_start_ms = timestamp_ms,
      .count = 1U,
    });
    return false;
  }

  if (timestamp_ms < iter->window_start_ms ||
      timestamp_ms - iter->window_start_ms >= route.rate_window_ms) {
    iter->window_start_ms = timestamp_ms;
    iter->count = 1U;
    return false;
  }

  if (iter->count >= route.max_requests_per_window) {
    return true;
  }

  ++iter->count;
  return false;
}

ApiReceipt AutomotiveApiGateway::MakeReceipt(
  const ApiRoute* route,
  const ClientContext& client,
  const ApiRequest& request,
  GatewayReceiptStatus status,
  std::string reason) const {
  return {
    .status = status,
    .route_id = route == nullptr ? request.route_id : route->route_id,
    .client_id = ClientId(client),
    .service = route == nullptr ? com::ServiceIdentifier{} : route->service,
    .gateway_sequence = 0U,
    .service_sequence = 0U,
    .payload_size = request.payload.size(),
    .reason = std::move(reason),
    .audit_label = route == nullptr ? std::string{} : route->deployment_ref,
    .accepted = false,
  };
}

core::Result<ApiReceipt> AutomotiveApiGateway::Accept(ApiReceipt receipt) {
  receipt.accepted = true;
  receipt.gateway_sequence = next_sequence_++;
  ++accepted_requests_;
  RecordReceipt(receipt);
  return core::Result<ApiReceipt>::FromValue(std::move(receipt));
}

core::Result<ApiReceipt> AutomotiveApiGateway::Block(ApiReceipt receipt) {
  receipt.accepted = false;
  receipt.gateway_sequence = next_sequence_++;
  CountBlocked(receipt.status);
  const auto message = receipt.reason;
  RecordReceipt(std::move(receipt));
  return core::Result<ApiReceipt>::FromError({"automotive-api-gateway", message});
}

void AutomotiveApiGateway::RecordReceipt(ApiReceipt receipt) {
  if (policy_.max_receipts == 0U) {
    return;
  }

  receipts_.push_back(std::move(receipt));
  while (receipts_.size() > policy_.max_receipts) {
    receipts_.erase(receipts_.begin());
  }
}

void AutomotiveApiGateway::CountBlocked(GatewayReceiptStatus status) {
  ++rejected_requests_;
  if (status == GatewayReceiptStatus::kUnauthorized) {
    ++unauthorized_requests_;
  } else if (status == GatewayReceiptStatus::kRateLimited) {
    ++rate_limited_requests_;
  } else if (status == GatewayReceiptStatus::kPayloadRejected) {
    ++payload_rejected_requests_;
  } else if (status == GatewayReceiptStatus::kServiceUnavailable) {
    ++service_unavailable_requests_;
  }
}

void AutomotiveApiGateway::RecordAuthorizationFailure(
  const ClientContext& client,
  const ApiRoute& route,
  std::string_view reason,
  std::uint64_t timestamp_ms) const {
  if (security_events_ == nullptr) {
    return;
  }

  static_cast<void>(security_events_->Record({
    .source = "automotive-api-gateway",
    .category = "authorization",
    .severity = security::SecuritySeverity::kWarning,
    .principal_id = ClientId(client),
    .resource_id = route.route_id,
    .operation = security::Operation::kInvokeGatewayRoute,
    .detail = std::string(reason),
    .timestamp_ms = timestamp_ms,
  }));
}

std::string_view ToString(GatewayProtocol protocol) noexcept {
  switch (protocol) {
    case GatewayProtocol::kRestJson:
      return "RestJson";
    case GatewayProtocol::kGrpc:
      return "Grpc";
    case GatewayProtocol::kWebSocket:
      return "WebSocket";
    case GatewayProtocol::kMqtt:
      return "Mqtt";
    case GatewayProtocol::kTestLoopback:
      return "TestLoopback";
  }

  return "Unknown";
}

std::string_view ToString(GatewayOperation operation) noexcept {
  switch (operation) {
    case GatewayOperation::kDiscoverService:
      return "DiscoverService";
    case GatewayOperation::kPublishEvent:
      return "PublishEvent";
  }

  return "Unknown";
}

std::string_view ToString(GatewayReceiptStatus status) noexcept {
  switch (status) {
    case GatewayReceiptStatus::kAccepted:
      return "Accepted";
    case GatewayReceiptStatus::kRouteRejected:
      return "RouteRejected";
    case GatewayReceiptStatus::kUnauthorized:
      return "Unauthorized";
    case GatewayReceiptStatus::kRateLimited:
      return "RateLimited";
    case GatewayReceiptStatus::kPayloadRejected:
      return "PayloadRejected";
    case GatewayReceiptStatus::kServiceUnavailable:
      return "ServiceUnavailable";
  }

  return "Unknown";
}

}  // namespace openautosar::gateway
