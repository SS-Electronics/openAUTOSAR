// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/socketcan_endpoint.h"
#include "openautosar/virtual_vehicle/socketcan_ultrasonic_frame.h"
#include "openautosar/virtual_vehicle/ultrasonic_sensor_model.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Options final {
  std::uint32_t samples{5U};
  std::uint32_t fault_at{2U};
  std::string can_interface;
  bool dry_run{true};
  openautosar::virtual_vehicle::UltrasonicFault injected_fault{
    openautosar::virtual_vehicle::UltrasonicFault::kNone};
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
    if (argument == "--fault-at" && index + 1 < argc) {
      ++index;
      options.fault_at = static_cast<std::uint32_t>(std::stoul(argv[index]));
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
    if (argument == "--can-iface" && index + 1 < argc) {
      ++index;
      options.can_interface = argv[index];
      options.dry_run = false;
      continue;
    }
    if (argument == "--dry-run") {
      options.dry_run = true;
      continue;
    }

    std::cerr << "unknown argument: " << argument << '\n';
    std::exit(2);
  }

  return options;
}

void PrintFrame(std::uint32_t index, const canfd_frame& frame) {
  std::cout << "sample=" << index << " can_id=0x" << std::hex << std::setw(3)
            << std::setfill('0') << frame.can_id << std::dec << std::setfill(' ')
            << " len=" << static_cast<unsigned>(frame.len) << " payload=";

  for (std::uint8_t byte_index = 0U; byte_index < frame.len; ++byte_index) {
    if (byte_index > 0U) {
      std::cout << ' ';
    }
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned>(frame.data[byte_index]);
  }

  std::cout << std::dec << std::setfill(' ') << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  using namespace openautosar::virtual_vehicle;

  const auto options = ParseOptions(argc, argv);
  DeterministicUltrasonicModel model({
    .sensor_id = 1U,
    .scenario = UltrasonicScenario::kNormalApproach,
    .start_distance_mm = 1'500U,
    .step_mm = 75U,
    .period_ns = 10'000'000ULL,
  });

  std::optional<SocketCanEndpoint> endpoint;
  if (!options.dry_run) {
    auto opened = SocketCanEndpoint::Open(options.can_interface);
    if (!opened) {
      std::cerr << "failed to open SocketCAN interface: " << opened.Error().message << '\n';
      return 2;
    }
    endpoint.emplace(std::move(opened.Value()));
  }

  for (std::uint32_t index = 0U; index < options.samples; ++index) {
    const auto active_fault = index == options.fault_at ? options.injected_fault : UltrasonicFault::kNone;
    auto sample = model.Next(active_fault);

    if (!sample.has_value()) {
      std::cout << "sample=" << index << " dropped=true\n";
      continue;
    }

    const auto frame = EncodeSocketCanUltrasonicFrame(sample.value(), active_fault);
    PrintFrame(index, frame);

    if (endpoint.has_value()) {
      auto sent = endpoint->Send(frame);
      if (!sent) {
        std::cerr << "failed to send SocketCAN frame: " << sent.Error().message << '\n';
        return 3;
      }
    }
  }

  return 0;
}
