// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/classic_gateway_config.h"
#include "openautosar/virtual_vehicle/socketcan_ultrasonic_frame.h"
#include "openautosar/virtual_vehicle/ultrasonic_diagnostic_bridge.h"
#include "openautosar/virtual_vehicle/ultrasonic_sensor_model.h"
#include "openautosar/virtual_vehicle/ultrasonic_signal_validator.h"
#include "openautosar/com/service_registry.h"
#include "openautosar/runtime/diagnostic_manager.h"
#include "openautosar/runtime/platform_health_manager.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

struct Options final {
  std::uint32_t samples{5U};
  openautosar::virtual_vehicle::UltrasonicFault injected_fault{
    openautosar::virtual_vehicle::UltrasonicFault::kNone};
  std::uint32_t fault_at{2U};
  std::string gateway_config;
};

std::optional<openautosar::virtual_vehicle::UltrasonicFault> ParseFault(std::string_view value) {
  using openautosar::virtual_vehicle::UltrasonicFault;

  if (value == "none") {
    return UltrasonicFault::kNone;
  }
  if (value == "no_echo") {
    return UltrasonicFault::kNoEcho;
  }
  if (value == "multipath_noise") {
    return UltrasonicFault::kMultipathNoise;
  }
  if (value == "frozen_sample") {
    return UltrasonicFault::kFrozenSample;
  }
  if (value == "counter_jump") {
    return UltrasonicFault::kCounterJump;
  }
  if (value == "crc_error") {
    return UltrasonicFault::kCrcError;
  }
  if (value == "delayed_frame") {
    return UltrasonicFault::kDelayedFrame;
  }
  if (value == "dropped_frame") {
    return UltrasonicFault::kDroppedFrame;
  }
  if (value == "out_of_range") {
    return UltrasonicFault::kOutOfRange;
  }
  if (value == "sensor_reset") {
    return UltrasonicFault::kSensorReset;
  }

  return std::nullopt;
}

Options ParseOptions(int argc, char** argv) {
  Options options;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--samples" && index + 1 < argc) {
      ++index;
      options.samples = static_cast<std::uint32_t>(std::stoul(argv[index]));
      continue;
    }
    if (argument == "--fault" && index + 1 < argc) {
      ++index;
      auto fault = ParseFault(argv[index]);
      if (!fault.has_value()) {
        std::cerr << "unsupported fault: " << argv[index] << '\n';
        std::exit(2);
      }
      options.injected_fault = fault.value();
      continue;
    }
    if (argument == "--fault-at" && index + 1 < argc) {
      ++index;
      options.fault_at = static_cast<std::uint32_t>(std::stoul(argv[index]));
      continue;
    }
    if (argument == "--gateway-config" && index + 1 < argc) {
      ++index;
      options.gateway_config = argv[index];
      continue;
    }

    std::cerr << "unknown argument: " << argument << '\n';
    std::exit(2);
  }

  return options;
}

openautosar::core::Result<openautosar::virtual_vehicle::ClassicGatewayConfig>
LoadGatewayConfig(const std::string& path) {
  using openautosar::virtual_vehicle::ClassicGatewayConfig;
  using openautosar::virtual_vehicle::DefaultClassicGatewayConfig;
  using openautosar::virtual_vehicle::ParseClassicGatewayRuntimeConfig;

  if (path.empty()) {
    return openautosar::core::Result<ClassicGatewayConfig>::FromValue(
      DefaultClassicGatewayConfig());
  }

  std::ifstream input{path};
  if (!input) {
    return openautosar::core::Result<ClassicGatewayConfig>::FromError(
      {"classic-gateway-config", "failed to open runtime config"});
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return ParseClassicGatewayRuntimeConfig(buffer.str());
}

void PrintReport(
  std::uint32_t index,
  const openautosar::virtual_vehicle::UltrasonicValidationReport& report) {
  using openautosar::virtual_vehicle::ToString;

  std::cout << "sample=" << index << " state=" << ToString(report.state) << " distance=";
  if (report.publish_distance_mm.has_value()) {
    std::cout << report.publish_distance_mm.value();
  } else {
    std::cout << "none";
  }

  std::cout << " faults=";
  if (report.faults.empty()) {
    std::cout << "none";
  } else {
    for (std::size_t fault_index = 0U; fault_index < report.faults.size(); ++fault_index) {
      if (fault_index > 0U) {
        std::cout << ',';
      }
      std::cout << ToString(report.faults[fault_index]);
    }
  }

  std::cout << " degraded_request="
            << (report.request_degraded_state ? "true" : "false") << '\n';
}

std::vector<std::uint8_t> PayloadFromFrame(const canfd_frame& frame) {
  return std::vector<std::uint8_t>(frame.data, frame.data + frame.len);
}

std::uint64_t TimestampMsForIndex(std::uint32_t index) noexcept {
  return static_cast<std::uint64_t>(index) * 10ULL;
}

void PrintHealth(const openautosar::runtime::phm::SupervisionReport& report) {
  std::cout << "phm_health=" << openautosar::runtime::phm::ToString(report.health)
            << " phm_status=" << openautosar::runtime::phm::ToString(report.status)
            << " phm_action=" << openautosar::runtime::phm::ToString(report.action) << '\n';
}

openautosar::runtime::diagnostics::UdsMessage ReadDtcRecordsRequest() {
  namespace diag = openautosar::runtime::diagnostics;

  return {
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDtcInformation),
    .payload = {0x02U, 0xFFU},
  };
}

std::size_t CountDtcRecords(const openautosar::runtime::diagnostics::UdsMessage& response) {
  if (response.service_id != 0x59U || response.payload.size() < 2U) {
    return 0U;
  }

  return (response.payload.size() - 2U) / 4U;
}

bool PrintDiagnosticCanEvidence(
  openautosar::runtime::diagnostics::DiagnosticManager& diagnostics) {
  namespace diag = openautosar::runtime::diagnostics;

  auto encoded = diag::UdsCanFrameCodec::EncodeSingleFrame(
    ReadDtcRecordsRequest(),
    diag::kDefaultTesterRequestCanId);
  if (!encoded) {
    std::cerr << "failed to encode UDS DTC request: " << encoded.Error().message << '\n';
    return false;
  }

  auto response_frame = diagnostics.HandleCanRequest(encoded.Value());
  if (!response_frame) {
    std::cerr << "failed to handle UDS DTC request: " << response_frame.Error().message << '\n';
    return false;
  }

  auto decoded = diag::UdsCanFrameCodec::DecodeSingleFrame(
    response_frame.Value(),
    diag::kDefaultServerResponseCanId);
  if (!decoded) {
    std::cerr << "failed to decode UDS DTC response: " << decoded.Error().message << '\n';
    return false;
  }

  const auto previous_flags = std::cout.flags();
  std::cout << "uds_response_can_id=0x" << std::hex << response_frame.Value().can_id
            << " uds_sid=0x" << static_cast<unsigned>(decoded.Value().service_id)
            << std::dec << " uds_dtc_records=" << CountDtcRecords(decoded.Value())
            << " diagnostic_event_memory=" << diagnostics.EventMemory().size() << '\n';
  std::cout.flags(previous_flags);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace openautosar::virtual_vehicle;

  const auto options = ParseOptions(argc, argv);
  auto loaded_config = LoadGatewayConfig(options.gateway_config);
  if (!loaded_config) {
    std::cerr << "failed to load gateway config: " << loaded_config.Error().message << '\n';
    return 14;
  }
  const auto& config = loaded_config.Value();
  const openautosar::com::ServiceIdentifier ultrasonic_service{
    .interface_id = config.service_interface_id,
    .instance_id = config.service_instance_id,
    .major_version = config.service_major_version,
    .minor_version = config.service_minor_version,
  };

  openautosar::com::ServiceRegistry registry;
  openautosar::runtime::diagnostics::DiagnosticManager diagnostics;
  UltrasonicDiagnosticBridge diagnostic_bridge{diagnostics};
  auto diagnostic_contribution = diagnostic_bridge.RegisterContribution();
  if (!diagnostic_contribution) {
    std::cerr << "failed to register diagnostic contribution: "
              << diagnostic_contribution.Error().message << '\n';
    return 11;
  }

  openautosar::runtime::phm::PlatformHealthManager health_manager;
  auto supervised = health_manager.RegisterEntity({
    .entity = config.process_identity,
    .checkpoint_sequence = {"Alive"},
    .alive_timeout = std::chrono::milliseconds(30),
    .deadline_timeout = std::chrono::milliseconds(25),
    .degraded_after_missed_alive = 1U,
    .failed_after_missed_alive = 3U,
    .max_restart_count = 1U,
    .activation_time_ms = 0U,
  });
  if (!supervised) {
    std::cerr << "failed to register PHM supervision: " << supervised.Error().message << '\n';
    return 2;
  }

  auto offered = registry.OfferService({
    .service = ultrasonic_service,
    .process_identity = config.process_identity,
    .machine_identity = config.machine_identity,
    .endpoint = {
      .binding = openautosar::com::Binding::kLocalIpc,
      .address = config.endpoint_address,
      .port = 0U,
    },
    .ttl_ms = 1'000U,
    .access_policy = config.access_policy,
    .deployment_provenance = config.deployment_provenance,
  });
  if (!offered) {
    std::cerr << "failed to offer ultrasonic service: " << offered.Error().message << '\n';
    return 2;
  }

  auto find_handle = registry.StartFind(ultrasonic_service);
  if (!find_handle) {
    std::cerr << "failed to start service discovery: " << find_handle.Error().message << '\n';
    return 2;
  }

  const auto found = registry.FindService(ultrasonic_service);
  if (found.empty()) {
    std::cerr << "ultrasonic service was not discoverable\n";
    return 3;
  }

  auto subscription = registry.Subscribe(ultrasonic_service, config.event_name, 2U);
  if (!subscription) {
    std::cerr << "failed to subscribe to ultrasonic event: " << subscription.Error().message
              << '\n';
    return 4;
  }

  DeterministicUltrasonicModel model({
    .sensor_id = 1U,
    .scenario = UltrasonicScenario::kNormalApproach,
    .start_distance_mm = 1'500U,
    .step_mm = 75U,
    .period_ns = 10'000'000ULL,
  });
  UltrasonicSignalValidator validator({.max_sample_age_ns = config.max_sample_age_ns});

  bool saw_injected_fault{options.injected_fault == UltrasonicFault::kNone};
  std::uint64_t delivered_events{0U};
  for (std::uint32_t index = 0U; index < options.samples; ++index) {
    const auto active_fault =
      index == options.fault_at ? options.injected_fault : UltrasonicFault::kNone;
    auto sample = model.Next(active_fault);
    UltrasonicValidationReport report;

    if (!sample.has_value()) {
      report = validator.ObserveDroppedFrame();
    } else {
      const auto frame = EncodeSocketCanUltrasonicFrame(sample.value(), active_fault);
      report = validator.ValidateSocketCanFrame(frame, sample.value().timestamp_ns + 1'000'000ULL);
      if (report.publish_distance_mm.has_value()) {
        auto sequence = registry.Publish({
          .service = ultrasonic_service,
          .event_name = config.event_name,
          .payload = PayloadFromFrame(frame),
        });
        if (!sequence) {
          std::cerr << "failed to publish ultrasonic event: " << sequence.Error().message << '\n';
          return 5;
        }

        auto delivered = registry.Poll(subscription.Value().id);
        if (!delivered) {
          std::cerr << "published ultrasonic event was not delivered: " << delivered.Error().message
                    << '\n';
          return 6;
        }
        ++delivered_events;
        std::cout << "event_sequence=" << delivered.Value().sequence << " event_payload="
                  << delivered.Value().payload.size() << '\n';

        auto health = health_manager.ReportCheckpoint(
          config.process_identity,
          "Alive",
          TimestampMsForIndex(index));
        if (!health) {
          std::cerr << "failed to report PHM checkpoint: " << health.Error().message << '\n';
          return 9;
        }
        PrintHealth(health.Value());
      }
    }

    if (report.state != UltrasonicServiceState::kAvailable) {
      auto health = health_manager.ReportServiceAvailability(
        config.process_identity,
        false,
        TimestampMsForIndex(index));
      if (!health) {
        std::cerr << "failed to report PHM service health: " << health.Error().message << '\n';
        return 10;
      }
      PrintHealth(health.Value());
    }

    auto diagnostic_update = diagnostic_bridge.ObserveValidationReport(report);
    if (!diagnostic_update) {
      std::cerr << "failed to update diagnostics: " << diagnostic_update.Error().message << '\n';
      return 12;
    }
    if (diagnostic_update.Value() > 0U) {
      std::cout << "diagnostic_reported_dtcs=" << diagnostic_update.Value()
                << " event_memory=" << diagnostics.EventMemory().size() << '\n';
    }

    if (!report.faults.empty()) {
      saw_injected_fault = true;
    }

    PrintReport(index, report);
  }

  auto stopped_find = registry.StopFind(find_handle.Value().id);
  if (!stopped_find) {
    std::cerr << "failed to stop service discovery: " << stopped_find.Error().message << '\n';
    return 7;
  }

  auto stopped_offer = registry.StopOffer(ultrasonic_service);
  if (!stopped_offer) {
    std::cerr << "failed to stop ultrasonic offer: " << stopped_offer.Error().message << '\n';
    return 8;
  }

  std::cout << "service_events=" << delivered_events << '\n';
  if (!PrintDiagnosticCanEvidence(diagnostics)) {
    return 13;
  }

  return saw_injected_fault ? 0 : 3;
}
