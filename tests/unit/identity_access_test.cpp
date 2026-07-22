// SPDX-License-Identifier: MIT

#include "openautosar/com/service_registry.h"
#include "openautosar/runtime/diagnostic_manager.h"
#include "openautosar/security/identity_access_manager.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

openautosar::security::Principal Principal(
  std::string_view application,
  std::string_view machine,
  std::vector<std::string> roles,
  bool remote = false) {
  openautosar::security::Principal principal;
  principal.application_id = application;
  principal.machine_id = machine;
  principal.security_label = "openautosar.test";
  principal.roles = std::move(roles);
  principal.authenticated = true;
  principal.remote = remote;
  return principal;
}

openautosar::security::AccessRule AllowRule(
  std::vector<openautosar::security::Operation> operations,
  std::vector<openautosar::security::ResourceKind> resource_kinds,
  std::vector<std::string> roles,
  bool allow_remote = false) {
  openautosar::security::AccessRule rule;
  rule.decision = openautosar::security::Decision::kAllow;
  rule.operations = std::move(operations);
  rule.resource_kinds = std::move(resource_kinds);
  rule.roles = std::move(roles);
  rule.require_authenticated = true;
  rule.allow_remote = allow_remote;
  rule.reason = "test allow rule";
  return rule;
}

void RequireNegativeUds(
  const openautosar::runtime::diagnostics::UdsMessage& response,
  openautosar::runtime::diagnostics::NegativeResponseCode expected) {
  Require(response.service_id == 0x7FU, "UDS response was not negative");
  Require(response.payload.size() == 2U, "UDS negative response payload changed");
  Require(
    response.payload[1U] == static_cast<std::uint8_t>(expected),
    "UDS negative response code changed");
}

}  // namespace

int main() {
  namespace com = openautosar::com;
  namespace diag = openautosar::runtime::diagnostics;
  namespace iam = openautosar::security;

  iam::SecurityPolicy policy;
  policy.production_mode = true;
  policy.remote_diagnostics_allowed = false;
  policy.default_decision = iam::Decision::kDeny;
  policy.rules.push_back(AllowRule(
    {iam::Operation::kOfferService, iam::Operation::kPublishEvent},
    {iam::ResourceKind::kService},
    {"ultrasonic-provider"}));
  policy.rules.push_back(AllowRule(
    {iam::Operation::kFindService, iam::Operation::kSubscribeEvent},
    {iam::ResourceKind::kService},
    {"ultrasonic-consumer"}));

  iam::AccessPolicyEngine access{policy};
  const auto provider = Principal(
    "oa-ultrasonic-gateway-smoke",
    "qemux86-64",
    {"ultrasonic-provider"});
  const auto consumer = Principal(
    "oa-dashboard",
    "qemux86-64",
    {"ultrasonic-consumer"});
  const auto intruder = Principal("unknown", "qemux86-64", {"none"});

  const com::ServiceIdentifier service{
    .interface_id = 0x0A500001U,
    .instance_id = 1U,
    .major_version = 1U,
    .minor_version = 0U,
  };
  com::ServiceOffer offer;
  offer.service = service;
  offer.process_identity = "oa-ultrasonic-gateway-smoke";
  offer.machine_identity = "qemux86-64";
  offer.endpoint = {.binding = com::Binding::kLocalIpc, .address = "local://ultrasonic"};
  offer.ttl_ms = 1'000U;
  offer.access_policy = "ultrasonic-service";
  offer.deployment_provenance = "model/examples/vehicle/ultrasonic_service.json";

  com::ServiceRegistry registry{access};
  iam::SecurityEventCollector collector{{.max_events = 8U, .aggregation_window_ms = 100U}};
  registry.SetSecurityEventCollector(collector);
  Require(!registry.OfferService(offer).HasValue(), "anonymous service offer was allowed");
  Require(registry.OfferServiceAs(provider, offer).HasValue(), "provider offer was denied");
  Require(!registry.StartFindAs(intruder, service).HasValue(), "intruder find was allowed");
  Require(registry.StartFindAs(consumer, service).HasValue(), "consumer find was denied");
  Require(
    !registry.SubscribeAs(intruder, service, "DistanceSample", 2U).HasValue(),
    "intruder subscription was allowed");
  auto subscription = registry.SubscribeAs(consumer, service, "DistanceSample", 2U);
  Require(subscription.HasValue(), "consumer subscription was denied");
  Require(
    !registry.PublishAs(intruder, {.service = service, .event_name = "DistanceSample",
                                   .payload = {0x01U}}).HasValue(),
    "intruder publish was allowed");
  auto sequence = registry.PublishAs(
    provider,
    {.service = service, .event_name = "DistanceSample", .payload = {0x02U}});
  Require(sequence.HasValue(), "provider publish was denied");
  Require(registry.Poll(subscription.Value().id).HasValue(), "authorized sample missing");

  diag::DiagnosticManager denied_diagnostics;
  auto diagnostic_policy = policy;
  diagnostic_policy.rules.push_back(AllowRule(
    {iam::Operation::kDiagnosticRead,
     iam::Operation::kDiagnosticSecurityAccess,
     iam::Operation::kDiagnosticClearDtc},
    {iam::ResourceKind::kDiagnostic},
    {"diagnostic-tester"},
    true));
  iam::AccessPolicyEngine remote_denied{diagnostic_policy};
  denied_diagnostics.SetAuthorizationPolicy(
    remote_denied,
    Principal("remote-tester", "qemux86-64", {"diagnostic-tester"}, true));
  denied_diagnostics.SetSecurityEventCollector(collector);

  auto denied_read = denied_diagnostics.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDataByIdentifier),
    .payload = {0xF1U, 0x89U},
  });
  Require(denied_read.HasValue(), "denied diagnostic read returned transport error");
  RequireNegativeUds(
    denied_read.Value(),
    diag::NegativeResponseCode::kSecurityAccessDenied);
  Require(
    collector.CountBySeverity(iam::SecuritySeverity::kCritical) >= 2U,
    "authorization failures were not reported to IDSM collector");

  diagnostic_policy.remote_diagnostics_allowed = true;
  iam::AccessPolicyEngine remote_allowed{diagnostic_policy};
  diag::DiagnosticManager allowed_diagnostics;
  allowed_diagnostics.SetAuthorizationPolicy(
    remote_allowed,
    Principal("remote-tester", "qemux86-64", {"diagnostic-tester"}, true));

  auto allowed_read = allowed_diagnostics.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDataByIdentifier),
    .payload = {0xF1U, 0x89U},
  });
  Require(allowed_read.HasValue(), "authorized diagnostic read failed");
  Require(allowed_read.Value().service_id == 0x62U, "authorized diagnostic read was denied");

  auto key = allowed_diagnostics.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kSecurityAccess),
    .payload = {0x02U, 0xFFU, 0xFFU},
  });
  Require(key.HasValue(), "authorized security access failed");
  Require(key.Value().service_id == 0x67U, "authorized security access was denied");
  Require(allowed_diagnostics.SecurityUnlocked(), "diagnostic security did not unlock");

  Require(allowed_diagnostics.ReportDtc({
            .code = 0x0A5001U,
            .status = static_cast<std::uint8_t>(diag::DtcStatus::kTestFailed),
            .origin = "iam-test",
            .description = "security-authorized clear",
          }).HasValue(),
          "DTC report failed");
  auto clear = allowed_diagnostics.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kClearDiagnosticInformation),
    .payload = {0xFFU, 0xFFU, 0xFFU},
  });
  Require(clear.HasValue(), "authorized clear-DTC failed");
  Require(clear.Value().service_id == 0x54U, "authorized clear-DTC was denied");

  iam::TrustStore trust_store;
  Require(!trust_store.TrustSigner({}).HasValue(), "invalid signer trust was accepted");
  iam::SignerTrust trust;
  trust.signer_id = "openautosar-dev";
  trust.key_slot = "slot://dev-signing";
  trust.certificate_id = "cert-openautosar-dev";
  trust.minimum_counter = 7U;
  Require(trust_store.TrustSigner(trust).HasValue(), "valid signer trust was rejected");
  Require(!trust_store.IsSignerTrusted("openautosar-dev", 6U), "rollback counter was ignored");
  Require(trust_store.IsSignerTrusted("openautosar-dev", 7U), "trusted signer was rejected");
  Require(trust_store.RevokeSigner("openautosar-dev").HasValue(), "signer revoke failed");
  Require(!trust_store.IsSignerTrusted("openautosar-dev", 8U), "revoked signer was trusted");

  iam::SecurityEventCollector aggregate_collector{
    {.max_events = 4U, .aggregation_window_ms = 100U}};
  iam::SecurityEvent event;
  event.source = "ara-com";
  event.category = "authorization";
  event.severity = iam::SecuritySeverity::kCritical;
  event.principal_id = "unknown";
  event.resource_id = "ultrasonic-service";
  event.operation = iam::Operation::kSubscribeEvent;
  event.detail = "subscription denied";
  event.timestamp_ms = 10U;
  Require(aggregate_collector.Record(event).HasValue(), "security event was rejected");
  event.timestamp_ms = 30U;
  Require(aggregate_collector.Record(event).HasValue(), "security event aggregation failed");
  Require(
    aggregate_collector.Snapshot().size() == 1U,
    "security events were not aggregated");
  Require(aggregate_collector.Snapshot()[0U].count == 2U, "security event count changed");
  Require(
    aggregate_collector.CountBySeverity(iam::SecuritySeverity::kCritical) == 2U,
    "critical security event count changed");

  Require(iam::ToString(iam::Decision::kAllow) == std::string_view("Allow"),
          "IAM decision text changed");
  Require(iam::ToString(iam::Operation::kDiagnosticClearDtc) ==
            std::string_view("DiagnosticClearDtc"),
          "IAM operation text changed");

  return 0;
}
