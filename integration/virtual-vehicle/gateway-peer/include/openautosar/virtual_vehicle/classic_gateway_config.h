// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/virtual_vehicle/socketcan_ultrasonic_frame.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openautosar::virtual_vehicle {

struct ClassicGatewayConfig final {
  std::string can_interface{"vcan0"};
  canid_t ultrasonic_can_id{kClassicUltrasonicCanId};
  std::size_t ultrasonic_pdu_size{kClassicUltrasonicPduSize};
  std::uint32_t service_interface_id{0x0A500001U};
  std::uint32_t service_instance_id{0x00000001U};
  std::uint16_t service_major_version{1U};
  std::uint16_t service_minor_version{0U};
  std::string event_name{"DistanceSample"};
  std::string process_identity{"oa-ultrasonic-gateway-smoke"};
  std::string machine_identity{"qemux86-64"};
  std::string endpoint_address{"local://ultrasonic/front-center"};
  std::string access_policy{"local-test"};
  std::string deployment_provenance{"model/examples/vehicle/ultrasonic_service.json"};
  std::uint64_t max_sample_age_ns{25'000'000ULL};
};

[[nodiscard]] ClassicGatewayConfig DefaultClassicGatewayConfig();

[[nodiscard]] core::Result<ClassicGatewayConfig> ParseClassicGatewayRuntimeConfig(
  std::string_view text);

}  // namespace openautosar::virtual_vehicle
