// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/classic_gateway_config.h"

#include <charconv>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace openautosar::virtual_vehicle {
namespace {

[[nodiscard]] core::ErrorCode MakeError(std::string message) {
  return {"classic-gateway-config", std::move(message)};
}

[[nodiscard]] std::string Trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1U));
}

[[nodiscard]] core::Result<std::uint64_t> ParseUnsigned(
  const std::map<std::string, std::string>& values,
  std::string_view key) {
  const auto found = values.find(std::string(key));
  if (found == values.end() || found->second.empty()) {
    return core::Result<std::uint64_t>::FromError(
      MakeError("missing numeric key " + std::string(key)));
  }

  const auto& text = found->second;
  std::uint64_t value{0U};
  int base{10};
  std::string_view digits{text};
  if (digits.size() > 2U && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    base = 16;
    digits.remove_prefix(2U);
  }
  const auto* begin = digits.data();
  const auto* end = digits.data() + digits.size();
  const auto result = std::from_chars(begin, end, value, base);
  if (result.ec != std::errc{} || result.ptr != end) {
    return core::Result<std::uint64_t>::FromError(
      MakeError("invalid numeric key " + std::string(key)));
  }
  return core::Result<std::uint64_t>::FromValue(value);
}

[[nodiscard]] core::Result<std::string> RequireString(
  const std::map<std::string, std::string>& values,
  std::string_view key) {
  const auto found = values.find(std::string(key));
  if (found == values.end() || found->second.empty()) {
    return core::Result<std::string>::FromError(
      MakeError("missing string key " + std::string(key)));
  }
  return core::Result<std::string>::FromValue(found->second);
}

[[nodiscard]] core::Result<bool> RequireSchema(
  const std::map<std::string, std::string>& values) {
  const auto schema = RequireString(values, "schema");
  if (!schema) {
    return core::Result<bool>::FromError(schema.Error());
  }
  if (schema.Value() != "openautosar.classic-gateway.runtime.v1") {
    return core::Result<bool>::FromError(MakeError("unsupported runtime schema"));
  }
  return core::Result<bool>::FromValue(true);
}

}  // namespace

ClassicGatewayConfig DefaultClassicGatewayConfig() {
  return {};
}

core::Result<ClassicGatewayConfig> ParseClassicGatewayRuntimeConfig(std::string_view text) {
  std::map<std::string, std::string> values;
  std::istringstream stream{std::string(text)};
  std::string line;
  while (std::getline(stream, line)) {
    const auto comment = line.find('#');
    if (comment != std::string::npos) {
      line.erase(comment);
    }
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      if (!Trim(line).empty()) {
        return core::Result<ClassicGatewayConfig>::FromError(
          MakeError("malformed runtime config line"));
      }
      continue;
    }
    values.emplace(Trim(std::string_view(line).substr(0U, separator)),
                   Trim(std::string_view(line).substr(separator + 1U)));
  }

  auto schema = RequireSchema(values);
  if (!schema) {
    return core::Result<ClassicGatewayConfig>::FromError(schema.Error());
  }

  ClassicGatewayConfig config{};
  auto can_interface = RequireString(values, "can_interface");
  auto event_name = RequireString(values, "event_name");
  auto process = RequireString(values, "process_identity");
  auto machine = RequireString(values, "machine_identity");
  auto endpoint = RequireString(values, "endpoint_address");
  auto access_policy = RequireString(values, "access_policy");
  auto provenance = RequireString(values, "deployment_provenance");
  if (!can_interface) {
    return core::Result<ClassicGatewayConfig>::FromError(can_interface.Error());
  }
  if (!event_name) {
    return core::Result<ClassicGatewayConfig>::FromError(event_name.Error());
  }
  if (!process) {
    return core::Result<ClassicGatewayConfig>::FromError(process.Error());
  }
  if (!machine) {
    return core::Result<ClassicGatewayConfig>::FromError(machine.Error());
  }
  if (!endpoint) {
    return core::Result<ClassicGatewayConfig>::FromError(endpoint.Error());
  }
  if (!access_policy) {
    return core::Result<ClassicGatewayConfig>::FromError(access_policy.Error());
  }
  if (!provenance) {
    return core::Result<ClassicGatewayConfig>::FromError(provenance.Error());
  }

  auto can_id = ParseUnsigned(values, "ultrasonic_can_id");
  auto pdu_size = ParseUnsigned(values, "ultrasonic_pdu_size");
  auto interface_id = ParseUnsigned(values, "service_interface_id");
  auto instance_id = ParseUnsigned(values, "service_instance_id");
  auto major = ParseUnsigned(values, "service_major_version");
  auto minor = ParseUnsigned(values, "service_minor_version");
  auto max_age = ParseUnsigned(values, "max_sample_age_ns");
  if (!can_id) {
    return core::Result<ClassicGatewayConfig>::FromError(can_id.Error());
  }
  if (!pdu_size) {
    return core::Result<ClassicGatewayConfig>::FromError(pdu_size.Error());
  }
  if (!interface_id) {
    return core::Result<ClassicGatewayConfig>::FromError(interface_id.Error());
  }
  if (!instance_id) {
    return core::Result<ClassicGatewayConfig>::FromError(instance_id.Error());
  }
  if (!major) {
    return core::Result<ClassicGatewayConfig>::FromError(major.Error());
  }
  if (!minor) {
    return core::Result<ClassicGatewayConfig>::FromError(minor.Error());
  }
  if (!max_age) {
    return core::Result<ClassicGatewayConfig>::FromError(max_age.Error());
  }
  if (can_id.Value() != kClassicUltrasonicCanId) {
    return core::Result<ClassicGatewayConfig>::FromError(MakeError("CAN id mismatch"));
  }
  if (pdu_size.Value() != kClassicUltrasonicPduSize) {
    return core::Result<ClassicGatewayConfig>::FromError(MakeError("PDU size mismatch"));
  }

  config.can_interface = can_interface.Value();
  config.event_name = event_name.Value();
  config.process_identity = process.Value();
  config.machine_identity = machine.Value();
  config.endpoint_address = endpoint.Value();
  config.access_policy = access_policy.Value();
  config.deployment_provenance = provenance.Value();
  config.ultrasonic_can_id = static_cast<canid_t>(can_id.Value());
  config.ultrasonic_pdu_size = static_cast<std::size_t>(pdu_size.Value());
  config.service_interface_id = static_cast<std::uint32_t>(interface_id.Value());
  config.service_instance_id = static_cast<std::uint32_t>(instance_id.Value());
  config.service_major_version = static_cast<std::uint16_t>(major.Value());
  config.service_minor_version = static_cast<std::uint16_t>(minor.Value());
  config.max_sample_age_ns = max_age.Value();
  return core::Result<ClassicGatewayConfig>::FromValue(std::move(config));
}

}  // namespace openautosar::virtual_vehicle
