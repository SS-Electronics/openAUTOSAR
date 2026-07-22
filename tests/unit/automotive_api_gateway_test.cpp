// SPDX-License-Identifier: MIT

#include "openautosar/gateway/automotive_api_gateway.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

openautosar::com::ServiceIdentifier UltrasonicService() {
  return {
    .interface_id = 0x0A500001U,
    .instance_id = 1U,
    .major_version = 1U,
    .minor_version = 0U,
  };
}

openautosar::gateway::ApiRoute PublishRoute() {
  return {
    .route_id = "vehicle.ultrasonic.distance.publish",
    .path = "/api/v1/vehicle/ultrasonic/distance",
    .protocol = openautosar::gateway::GatewayProtocol::kRestJson,
    .operation = openautosar::gateway::GatewayOperation::kPublishEvent,
    .service = UltrasonicService(),
    .event_name = "DistanceSample",
    .access_policy = "gateway/ultrasonic",
    .deployment_ref = "model/examples/vehicle/api-gateway/ultrasonic-distance",
    .enabled = true,
    .allow_remote = true,
    .require_authenticated = true,
    .max_payload_bytes = 4U,
    .max_requests_per_window = 2U,
    .rate_window_ms = 1'000U,
  };
}

openautosar::gateway::ApiRoute DiscoveryRoute() {
  auto route = PublishRoute();
  route.route_id = "vehicle.ultrasonic.distance.discover";
  route.path = "/api/v1/vehicle/ultrasonic";
  route.operation = openautosar::gateway::GatewayOperation::kDiscoverService;
  route.event_name.clear();
  route.max_payload_bytes = 1U;
  return route;
}

openautosar::gateway::ClientContext Client(
  std::vector<std::string> roles = {"api-consumer"}) {
  return {
    .client_id = "external-hmi",
    .machine_id = "qemux86-64",
    .security_label = "openautosar.gateway.test",
    .roles = std::move(roles),
    .authenticated = true,
    .remote = true,
  };
}

openautosar::security::AccessRule AllowGatewayRule() {
  openautosar::security::AccessRule rule;
  rule.decision = openautosar::security::Decision::kAllow;
  rule.operations = {openautosar::security::Operation::kInvokeGatewayRoute};
  rule.resource_kinds = {openautosar::security::ResourceKind::kApiGateway};
  rule.resource_prefixes = {"gateway/"};
  rule.roles = {"api-consumer"};
  rule.require_authenticated = true;
  rule.allow_remote = true;
  rule.reason = "gateway route allowed";
  return rule;
}

}  // namespace

int main() {
  namespace com = openautosar::com;
  namespace gateway = openautosar::gateway;
  namespace iam = openautosar::security;

  com::ServiceRegistry registry;
  const auto offer = registry.OfferService({
    .service = UltrasonicService(),
    .process_identity = "oa-ultrasonic-gateway-smoke",
    .machine_identity = "qemux86-64",
    .endpoint = {.binding = com::Binding::kLocalIpc, .address = "local://ultrasonic"},
    .ttl_ms = 1'000U,
    .access_policy = "ultrasonic-service",
    .deployment_provenance = "automotive-api-gateway-test",
  });
  Require(offer.HasValue(), "gateway target service offer failed");

  const auto subscription = registry.Subscribe(UltrasonicService(), "DistanceSample", 2U);
  Require(subscription.HasValue(), "gateway test subscription failed");

  iam::SecurityPolicy policy;
  policy.production_mode = true;
  policy.default_decision = iam::Decision::kDeny;
  policy.rules.push_back(AllowGatewayRule());
  iam::AccessPolicyEngine access{policy};
  iam::SecurityEventCollector events{{.max_events = 8U, .aggregation_window_ms = 100U}};

  gateway::AutomotiveApiGateway api{
    registry,
    {
      .max_routes = 4U,
      .max_payload_bytes = 8U,
      .max_receipts = 8U,
      .require_authenticated_clients = true,
      .reject_remote_clients_by_default = true,
    },
  };
  api.SetAccessPolicy(access);
  api.SetSecurityEventCollector(events);

  Require(!api.RegisterRoute({}).HasValue(), "invalid API gateway route was accepted");
  auto publish_route = api.RegisterRoute(PublishRoute());
  Require(publish_route.HasValue(), "valid API gateway publish route was rejected");
  Require(!api.RegisterRoute(PublishRoute()).HasValue(), "duplicate gateway route was accepted");
  Require(api.RegisterRoute(DiscoveryRoute()).HasValue(), "gateway discovery route failed");

  const auto discovered = api.Dispatch(Client(), {
    .route_id = "vehicle.ultrasonic.distance.discover",
    .payload = {},
    .monotonic_ms = 100U,
    .correlation_id = "discover-1",
  });
  Require(discovered.HasValue(), "authorized gateway discovery was rejected");
  Require(discovered.Value().accepted, "gateway discovery receipt was not accepted");

  const std::vector<std::uint8_t> distance{0x01U, 0x2CU};
  auto first_publish = api.Dispatch(Client(), {
    .route_id = "vehicle.ultrasonic.distance.publish",
    .payload = distance,
    .monotonic_ms = 200U,
    .correlation_id = "publish-1",
  });
  Require(first_publish.HasValue(), "authorized gateway publish was rejected");
  Require(first_publish.Value().service_sequence == 1U, "gateway service sequence changed");

  const auto delivered = registry.Poll(subscription.Value().id);
  Require(delivered.HasValue(), "gateway-published sample was not delivered");
  Require(delivered.Value().payload == distance, "gateway-published payload changed");

  auto unauthorized = api.Dispatch(Client({"viewer"}), {
    .route_id = "vehicle.ultrasonic.distance.publish",
    .payload = distance,
    .monotonic_ms = 300U,
    .correlation_id = "publish-unauthorized",
  });
  Require(!unauthorized.HasValue(), "unauthorized gateway client was allowed");
  Require(
    events.CountBySeverity(iam::SecuritySeverity::kWarning) == 1U,
    "gateway authorization failure was not recorded");

  auto oversized = api.Dispatch(Client(), {
    .route_id = "vehicle.ultrasonic.distance.publish",
    .payload = {0x01U, 0x02U, 0x03U, 0x04U, 0x05U},
    .monotonic_ms = 400U,
    .correlation_id = "publish-oversized",
  });
  Require(!oversized.HasValue(), "oversized gateway payload was accepted");

  Require(api.Dispatch(Client(), {
            .route_id = "vehicle.ultrasonic.distance.publish",
            .payload = distance,
            .monotonic_ms = 500U,
            .correlation_id = "publish-2",
          }).HasValue(),
          "second gateway publish was rejected");
  Require(!api.Dispatch(Client(), {
            .route_id = "vehicle.ultrasonic.distance.publish",
            .payload = distance,
            .monotonic_ms = 600U,
            .correlation_id = "publish-rate-limited",
          }).HasValue(),
          "gateway route rate limit was not enforced");

  auto missing_route = DiscoveryRoute();
  missing_route.route_id = "vehicle.missing.discover";
  missing_route.path = "/api/v1/vehicle/missing";
  missing_route.service.instance_id = 99U;
  Require(api.RegisterRoute(missing_route).HasValue(), "missing-service route rejected");
  Require(!api.Dispatch(Client(), {
            .route_id = "vehicle.missing.discover",
            .payload = {},
            .monotonic_ms = 2'000U,
            .correlation_id = "missing-service",
          }).HasValue(),
          "gateway discovery accepted an unavailable service");

  Require(
    api.RemoveRoute("vehicle.missing.discover").HasValue(),
    "gateway route removal failed");
  Require(!api.Dispatch(Client(), {
            .route_id = "vehicle.missing.discover",
            .payload = {},
            .monotonic_ms = 3'000U,
            .correlation_id = "removed-route",
          }).HasValue(),
          "removed gateway route was still dispatched");

  const auto snapshot = api.Snapshot();
  Require(snapshot.routes.size() == 2U, "gateway route snapshot changed");
  Require(snapshot.accepted_requests == 3U, "gateway accepted count changed");
  Require(snapshot.unauthorized_requests == 1U, "gateway unauthorized count changed");
  Require(snapshot.rate_limited_requests == 1U, "gateway rate limit count changed");
  Require(
    snapshot.payload_rejected_requests == 1U,
    "gateway payload rejection count changed");
  Require(
    snapshot.service_unavailable_requests == 1U,
    "gateway service-unavailable count changed");
  Require(!snapshot.receipts.empty(), "gateway receipts were not retained");

  Require(
    gateway::ToString(gateway::GatewayProtocol::kRestJson) == std::string_view("RestJson"),
    "gateway protocol text changed");
  Require(
    gateway::ToString(gateway::GatewayOperation::kPublishEvent) ==
      std::string_view("PublishEvent"),
    "gateway operation text changed");
  Require(
    gateway::ToString(gateway::GatewayReceiptStatus::kRateLimited) ==
      std::string_view("RateLimited"),
    "gateway receipt text changed");
  Require(
    iam::ToString(iam::ResourceKind::kApiGateway) == std::string_view("ApiGateway"),
    "IAM gateway resource text changed");
  Require(
    iam::ToString(iam::Operation::kInvokeGatewayRoute) ==
      std::string_view("InvokeGatewayRoute"),
    "IAM gateway operation text changed");

  return 0;
}
