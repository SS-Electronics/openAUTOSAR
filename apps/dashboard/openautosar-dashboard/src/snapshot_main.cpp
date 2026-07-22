// SPDX-License-Identifier: MIT

#include "openautosar/dashboard/dashboard_snapshot.h"
#include "openautosar/virtual_vehicle/socketcan_ultrasonic_frame.h"
#include "openautosar/virtual_vehicle/ultrasonic_sensor_model.h"
#include "openautosar/virtual_vehicle/ultrasonic_signal_validator.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Options final {
  std::uint32_t samples{6U};
  openautosar::virtual_vehicle::UltrasonicFault injected_fault{
    openautosar::virtual_vehicle::UltrasonicFault::kCrcError};
  std::uint32_t fault_at{3U};
  std::optional<std::filesystem::path> output;
};

std::optional<openautosar::virtual_vehicle::UltrasonicFault> ParseFault(
  std::string_view value) {
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
    if (argument == "--output" && index + 1 < argc) {
      ++index;
      options.output = std::filesystem::path(argv[index]);
      continue;
    }

    std::cerr << "unknown argument: " << argument << '\n';
    std::exit(2);
  }

  return options;
}

std::uint64_t TimestampMsForIndex(std::uint32_t index) noexcept {
  return static_cast<std::uint64_t>(index) * 10ULL;
}

}  // namespace

int main(int argc, char** argv) {
  namespace dash = openautosar::dashboard;
  namespace diag = openautosar::runtime::diagnostics;
  namespace phm = openautosar::runtime::phm;
  namespace ucm = openautosar::runtime::ucm;
  using namespace openautosar::virtual_vehicle;

  const auto options = ParseOptions(argc, argv);

  diag::DiagnosticManager diagnostics;
  UltrasonicDiagnosticBridge diagnostic_bridge{diagnostics};
  auto contribution = diagnostic_bridge.RegisterContribution();
  if (!contribution) {
    std::cerr << "failed to register dashboard diagnostic contribution: "
              << contribution.Error().message << '\n';
    return 3;
  }

  phm::PlatformHealthManager health_manager;
  auto registered = health_manager.RegisterEntity({
    .entity = "oa-ultrasonic-gateway-smoke",
    .checkpoint_sequence = {"Alive"},
    .alive_timeout = std::chrono::milliseconds(30),
    .deadline_timeout = std::chrono::milliseconds(25),
    .degraded_after_missed_alive = 1U,
    .failed_after_missed_alive = 3U,
    .max_restart_count = 1U,
    .activation_time_ms = 0U,
  });
  if (!registered) {
    std::cerr << "failed to register PHM entity: " << registered.Error().message << '\n';
    return 4;
  }

  DeterministicUltrasonicModel model({
    .sensor_id = 1U,
    .scenario = UltrasonicScenario::kNormalApproach,
    .start_distance_mm = 1'500U,
    .step_mm = 75U,
    .period_ns = 10'000'000ULL,
  });
  UltrasonicSignalValidator validator({.max_sample_age_ns = 25'000'000ULL});

  std::uint64_t service_events{0U};
  auto latest_health = registered.Value();
  std::vector<dash::FaultHistoryItem> history;

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
    }

    if (report.publish_distance_mm.has_value()) {
      ++service_events;
      auto health = health_manager.ReportCheckpoint(
        "oa-ultrasonic-gateway-smoke",
        "Alive",
        TimestampMsForIndex(index));
      if (!health) {
        std::cerr << "failed to report PHM checkpoint: " << health.Error().message << '\n';
        return 5;
      }
      latest_health = health.Value();
    } else {
      auto health = health_manager.ReportServiceAvailability(
        "oa-ultrasonic-gateway-smoke",
        false,
        TimestampMsForIndex(index));
      if (!health) {
        std::cerr << "failed to report PHM service state: " << health.Error().message << '\n';
        return 6;
      }
      latest_health = health.Value();
    }

    auto observed = diagnostic_bridge.ObserveValidationReport(report);
    if (!observed) {
      std::cerr << "failed to update dashboard diagnostics: " << observed.Error().message
                << '\n';
      return 7;
    }

    for (const auto fault : report.faults) {
      history.push_back({
        .sample_index = index,
        .fault = std::string(ToString(fault)),
        .dtc = DtcForValidationFault(fault),
        .service_state = std::string(ToString(report.state)),
        .degraded_requested = report.request_degraded_state,
      });
    }
  }

  ucm::SlotStatus slots;
  slots.active_slot = "A";
  slots.inactive_slot = "B";
  slots.inactive_slot_free_bytes = 256U * 1024U * 1024U;

  const auto json = dash::ToJson(dash::BuildSnapshot(
    diagnostic_bridge.Snapshot(),
    service_events,
    history,
    latest_health,
    diagnostics,
    ucm::TransactionState::kIdle,
    slots));

  if (options.output.has_value()) {
    if (options.output->has_parent_path()) {
      std::filesystem::create_directories(options.output->parent_path());
    }
    std::ofstream file{options.output.value()};
    if (!file) {
      std::cerr << "failed to open dashboard output: " << options.output->string() << '\n';
      return 8;
    }
    file << json;
    return 0;
  }

  std::cout << json;
  return 0;
}
