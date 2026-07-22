// SPDX-License-Identifier: MIT

#include "openautosar/com/service_registry.h"

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

}  // namespace

int main() {
  using namespace openautosar::com;

  ServiceRegistry registry;
  const ServiceIdentifier service{
    .interface_id = 0x0A500001U,
    .instance_id = 0x00000001U,
    .major_version = 1U,
    .minor_version = 0U,
  };

  const auto invalid_offer = registry.OfferService({});
  Require(!invalid_offer.HasValue(), "invalid service offer was accepted");

  const auto invalid_find = registry.StartFind({});
  Require(!invalid_find.HasValue(), "invalid start-find was accepted");

  const auto offer = registry.OfferService({
    .service = service,
    .process_identity = "communication-test-provider",
    .machine_identity = "qemux86-64",
    .endpoint = {.binding = Binding::kLocalIpc, .address = "local://communication-test"},
    .ttl_ms = 250U,
    .access_policy = "unit-test",
    .deployment_provenance = "communication-test",
  });
  Require(offer.HasValue(), "valid service offer was rejected");
  Require(ToString(offer.Value().endpoint.binding) == std::string_view("local-ipc"), "binding text changed");

  const auto find_handle = registry.StartFind(service);
  Require(find_handle.HasValue(), "start-find failed");
  Require(registry.FindService(service).size() == 1U, "offered service was not discoverable");

  const auto subscription = registry.Subscribe(service, "DistanceSample", 2U);
  Require(subscription.HasValue(), "valid subscription failed");

  const auto sequence1 = registry.Publish({
    .service = service,
    .event_name = "DistanceSample",
    .payload = {1U},
  });
  const auto sequence2 = registry.Publish({
    .service = service,
    .event_name = "DistanceSample",
    .payload = {2U},
  });
  const auto sequence3 = registry.Publish({
    .service = service,
    .event_name = "DistanceSample",
    .payload = {3U},
  });
  Require(sequence1.HasValue() && sequence2.HasValue() && sequence3.HasValue(), "publish failed");

  const auto delivered2 = registry.Poll(subscription.Value().id);
  const auto delivered3 = registry.Poll(subscription.Value().id);
  const auto empty = registry.Poll(subscription.Value().id);
  Require(delivered2.HasValue() && delivered3.HasValue(), "bounded queue did not deliver samples");
  Require(delivered2.Value().sequence == sequence2.Value(), "bounded queue did not drop oldest sample");
  Require(delivered3.Value().sequence == sequence3.Value(), "latest sample sequence changed");
  Require(!empty.HasValue(), "empty queue poll succeeded");

  registry.InvalidateEndpoint(service);
  Require(registry.FindService(service).empty(), "invalidated endpoint remained discoverable");
  const auto publish_after_invalidate = registry.Publish({
    .service = service,
    .event_name = "DistanceSample",
    .payload = {4U},
  });
  Require(!publish_after_invalidate.HasValue(), "publish after endpoint invalidation succeeded");

  const auto stopped_find = registry.StopFind(find_handle.Value().id);
  Require(stopped_find.HasValue(), "stop-find failed");

  const auto unsubscribed = registry.Unsubscribe(subscription.Value().id);
  Require(unsubscribed.HasValue(), "unsubscribe failed");

  return 0;
}
