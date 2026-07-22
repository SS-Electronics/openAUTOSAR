// SPDX-License-Identifier: MIT

#include "openautosar/runtime/diagnostic_manager.h"
#include "openautosar/virtual_vehicle/socketcan_ultrasonic_frame.h"
#include "openautosar/virtual_vehicle/ultrasonic_diagnostic_bridge.h"
#include "openautosar/virtual_vehicle/ultrasonic_sensor_model.h"
#include "openautosar/virtual_vehicle/ultrasonic_signal_validator.h"

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

openautosar::runtime::diagnostics::UdsMessage SendCanUds(
  openautosar::runtime::diagnostics::DiagnosticManager& manager,
  openautosar::runtime::diagnostics::UdsMessage request) {
  namespace diag = openautosar::runtime::diagnostics;

  auto encoded = diag::UdsCanFrameCodec::EncodeSingleFrame(
    request,
    diag::kDefaultTesterRequestCanId);
  Require(encoded.HasValue(), "UDS request did not encode");

  auto response_frame = manager.HandleCanRequest(encoded.Value());
  Require(response_frame.HasValue(), "diagnostic manager did not handle CAN request");
  Require(
    response_frame.Value().can_id == diag::kDefaultServerResponseCanId,
    "diagnostic response CAN id changed");

  auto decoded = diag::UdsCanFrameCodec::DecodeSingleFrame(
    response_frame.Value(),
    diag::kDefaultServerResponseCanId);
  Require(decoded.HasValue(), "UDS response did not decode");
  return decoded.Value();
}

void RequirePositive(
  const openautosar::runtime::diagnostics::UdsMessage& response,
  std::uint8_t expected_sid) {
  Require(response.service_id == expected_sid, "unexpected positive response SID");
}

}  // namespace

int main() {
  namespace diag = openautosar::runtime::diagnostics;
  using namespace openautosar::virtual_vehicle;

  diag::DiagnosticManager diagnostics;
  UltrasonicDiagnosticBridge bridge{diagnostics};
  Require(bridge.RegisterContribution().HasValue(), "gateway diagnostic contribution failed");

  DeterministicUltrasonicModel model({
    .sensor_id = 4U,
    .scenario = UltrasonicScenario::kNormalApproach,
    .start_distance_mm = 1'200U,
    .step_mm = 50U,
    .period_ns = 10'000'000ULL,
  });
  UltrasonicSignalValidator validator({.max_sample_age_ns = 25'000'000ULL});

  const auto first_sample = model.Next();
  Require(first_sample.has_value(), "first ultrasonic sample missing");
  const auto valid_report = validator.ValidateSocketCanFrame(
    EncodeSocketCanUltrasonicFrame(first_sample.value()),
    first_sample.value().timestamp_ns + 1'000'000ULL);
  Require(valid_report.state == UltrasonicServiceState::kAvailable, "valid sample not available");
  Require(bridge.ObserveValidationReport(valid_report).HasValue(), "valid report not observed");

  auto last_distance = SendCanUds(diagnostics, {
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDataByIdentifier),
    .payload = {
      static_cast<std::uint8_t>(kUltrasonicDidLastValidDistance >> 8U),
      static_cast<std::uint8_t>(kUltrasonicDidLastValidDistance & 0xFFU),
    },
  });
  RequirePositive(last_distance, 0x62U);
  Require(last_distance.payload.size() == 4U, "last-distance DID payload size changed");
  Require(last_distance.payload[2U] == 0x04U, "last-distance high byte changed");
  Require(last_distance.payload[3U] == 0xB0U, "last-distance low byte changed");

  const auto crc_sample = model.Next(UltrasonicFault::kCrcError);
  Require(crc_sample.has_value(), "CRC ultrasonic sample missing");
  const auto crc_report = validator.ValidateSocketCanFrame(
    EncodeSocketCanUltrasonicFrame(crc_sample.value(), UltrasonicFault::kCrcError),
    crc_sample.value().timestamp_ns + 1'000'000ULL);
  Require(ContainsFault(crc_report, ValidationFault::kCrcMismatch), "CRC fault not detected");

  auto reported = bridge.ObserveValidationReport(crc_report);
  Require(reported.HasValue(), "CRC report was not mapped to diagnostics");
  Require(reported.Value() == 1U, "CRC report mapped wrong DTC count");
  Require(diagnostics.EventMemory().size() == 1U, "DTC event memory size changed");
  Require(
    diagnostics.EventMemory()[0U].code == kUltrasonicDtcCrcMismatch,
    "CRC DTC code changed");

  auto dtc_records = SendCanUds(diagnostics, {
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDtcInformation),
    .payload = {0x02U, 0xFFU},
  });
  RequirePositive(dtc_records, 0x59U);
  Require(dtc_records.payload.size() == 6U, "DTC record payload size changed");
  Require(dtc_records.payload[2U] == 0x0AU, "DTC high byte changed");
  Require(dtc_records.payload[3U] == 0x50U, "DTC middle byte changed");
  Require(dtc_records.payload[4U] == 0x01U, "DTC low byte changed");

  auto session = SendCanUds(diagnostics, {
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kDiagnosticSessionControl),
    .payload = {static_cast<std::uint8_t>(diag::DiagnosticSession::kExtended)},
  });
  RequirePositive(session, 0x50U);

  auto seed = SendCanUds(diagnostics, {
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kSecurityAccess),
    .payload = {0x01U},
  });
  RequirePositive(seed, 0x67U);

  auto key = SendCanUds(diagnostics, {
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kSecurityAccess),
    .payload = {0x02U, 0xFFU, 0xFFU},
  });
  RequirePositive(key, 0x67U);

  auto clear_all = SendCanUds(diagnostics, {
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kClearDiagnosticInformation),
    .payload = {0xFFU, 0xFFU, 0xFFU},
  });
  RequirePositive(clear_all, 0x54U);
  Require(diagnostics.EventMemory().empty(), "DTCs were not cleared over UDS CAN path");

  UltrasonicSignalValidator invalid_validator;
  UltrasonicDiagnosticBridge empty_bridge{diagnostics};
  auto no_echo_sample = model.Next(UltrasonicFault::kNoEcho);
  Require(no_echo_sample.has_value(), "no-echo sample missing");
  const auto invalid_report = invalid_validator.ValidateSocketCanFrame(
    EncodeSocketCanUltrasonicFrame(no_echo_sample.value()),
    no_echo_sample.value().timestamp_ns);
  Require(empty_bridge.ObserveValidationReport(invalid_report).HasValue(),
          "invalid report without last distance was not observed");
  Require(
    empty_bridge.Snapshot().last_valid_distance_mm == std::nullopt,
    "invalid report created a last valid distance");

  return 0;
}
