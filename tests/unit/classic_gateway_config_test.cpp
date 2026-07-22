// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/classic_gateway_config.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Require(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "test failed: " << message << '\n';
  std::exit(1);
}

constexpr std::string_view kRuntimeConfig{
  "schema=openautosar.classic-gateway.runtime.v1\n"
  "tool_version=test\n"
  "can_interface=vcan0\n"
  "ultrasonic_can_id=0x321\n"
  "ultrasonic_pdu_size=23\n"
  "service_interface_id=0x0A500001\n"
  "service_instance_id=0x00000001\n"
  "service_major_version=1\n"
  "service_minor_version=0\n"
  "event_name=DistanceSample\n"
  "process_identity=oa-ultrasonic-gateway-smoke\n"
  "machine_identity=qemux86-64\n"
  "endpoint_address=local://ultrasonic/front-center\n"
  "access_policy=local-test\n"
  "deployment_provenance=model/examples/vehicle/ultrasonic_service.json\n"
  "max_sample_age_ns=25000000\n"};

}  // namespace

int main() {
  using namespace openautosar::virtual_vehicle;

  const auto parsed = ParseClassicGatewayRuntimeConfig(kRuntimeConfig);
  Require(parsed.HasValue(), "valid runtime config was rejected");
  Require(parsed.Value().can_interface == "vcan0", "CAN interface changed");
  Require(parsed.Value().ultrasonic_can_id == kClassicUltrasonicCanId, "CAN ID changed");
  Require(parsed.Value().ultrasonic_pdu_size == kClassicUltrasonicPduSize, "PDU size changed");
  Require(parsed.Value().service_interface_id == 0x0A500001U, "service interface changed");
  Require(parsed.Value().service_instance_id == 1U, "service instance changed");
  Require(parsed.Value().event_name == "DistanceSample", "event name changed");
  Require(parsed.Value().max_sample_age_ns == 25'000'000ULL, "max sample age changed");

  const auto missing = ParseClassicGatewayRuntimeConfig(
    "schema=openautosar.classic-gateway.runtime.v1\n"
    "ultrasonic_can_id=0x321\n");
  Require(!missing.HasValue(), "missing runtime keys were accepted");

  const auto wrong_can = ParseClassicGatewayRuntimeConfig(
    "schema=openautosar.classic-gateway.runtime.v1\n"
    "can_interface=vcan0\n"
    "ultrasonic_can_id=0x123\n"
    "ultrasonic_pdu_size=23\n"
    "service_interface_id=0x0A500001\n"
    "service_instance_id=0x00000001\n"
    "service_major_version=1\n"
    "service_minor_version=0\n"
    "event_name=DistanceSample\n"
    "process_identity=oa-ultrasonic-gateway-smoke\n"
    "machine_identity=qemux86-64\n"
    "endpoint_address=local://ultrasonic/front-center\n"
    "access_policy=local-test\n"
    "deployment_provenance=model/examples/vehicle/ultrasonic_service.json\n"
    "max_sample_age_ns=25000000\n");
  Require(!wrong_can.HasValue(), "wrong CAN ID was accepted");

  return 0;
}
