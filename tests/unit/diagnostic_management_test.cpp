// SPDX-License-Identifier: MIT

#include "openautosar/runtime/diagnostic_manager.h"

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

void RequirePositive(
  const openautosar::runtime::diagnostics::UdsMessage& response,
  std::uint8_t expected_sid) {
  Require(response.service_id == expected_sid, "unexpected UDS positive response SID");
}

void RequireNegative(
  const openautosar::runtime::diagnostics::UdsMessage& response,
  std::uint8_t request_sid,
  openautosar::runtime::diagnostics::NegativeResponseCode code) {
  Require(response.service_id == 0x7FU, "UDS response was not negative");
  Require(response.payload.size() == 2U, "UDS negative response payload size changed");
  Require(response.payload[0U] == request_sid, "UDS negative response request SID changed");
  Require(
    response.payload[1U] == static_cast<std::uint8_t>(code),
    "UDS negative response code changed");
}

}  // namespace

int main() {
  namespace diag = openautosar::runtime::diagnostics;

  diag::DiagnosticManager manager;

  auto invalid_frame =
    diag::UdsCanFrameCodec::EncodeSingleFrame({}, diag::kDefaultTesterRequestCanId);
  Require(!invalid_frame.HasValue(), "zero UDS service id was encoded");

  const diag::UdsMessage session_request{
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kDiagnosticSessionControl),
    .payload = {static_cast<std::uint8_t>(diag::DiagnosticSession::kExtended)},
  };
  auto encoded = diag::UdsCanFrameCodec::EncodeSingleFrame(
    session_request,
    diag::kDefaultTesterRequestCanId);
  Require(encoded.HasValue(), "UDS CAN single frame encode failed");

  auto decoded = diag::UdsCanFrameCodec::DecodeSingleFrame(
    encoded.Value(),
    diag::kDefaultTesterRequestCanId);
  Require(decoded.HasValue(), "UDS CAN single frame decode failed");
  Require(decoded.Value() == session_request, "UDS CAN frame roundtrip changed");

  auto wrong_route = diag::UdsCanFrameCodec::DecodeSingleFrame(encoded.Value(), 0x7E1U);
  Require(!wrong_route.HasValue(), "wrong diagnostic CAN route was accepted");

  auto session_response = manager.HandleRequest(session_request);
  Require(session_response.HasValue(), "diagnostic session control failed");
  RequirePositive(session_response.Value(), 0x50U);
  Require(manager.Session() == diag::DiagnosticSession::kExtended, "diagnostic session changed");

  auto read_version = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDataByIdentifier),
    .payload = {0xF1U, 0x89U},
  });
  Require(read_version.HasValue(), "ReadDataByIdentifier failed");
  RequirePositive(read_version.Value(), 0x62U);
  Require(read_version.Value().payload[0U] == 0xF1U, "DID high byte changed");
  Require(read_version.Value().payload[1U] == 0x89U, "DID low byte changed");

  auto unknown_did = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDataByIdentifier),
    .payload = {0x12U, 0x34U},
  });
  Require(unknown_did.HasValue(), "unknown DID request failed unexpectedly");
  RequireNegative(
    unknown_did.Value(),
    static_cast<std::uint8_t>(diag::UdsService::kReadDataByIdentifier),
    diag::NegativeResponseCode::kRequestOutOfRange);

  auto clear_without_security = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kClearDiagnosticInformation),
    .payload = {0xFFU, 0xFFU, 0xFFU},
  });
  Require(clear_without_security.HasValue(), "clear without security failed unexpectedly");
  RequireNegative(
    clear_without_security.Value(),
    static_cast<std::uint8_t>(diag::UdsService::kClearDiagnosticInformation),
    diag::NegativeResponseCode::kSecurityAccessDenied);

  auto seed = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kSecurityAccess),
    .payload = {0x01U},
  });
  Require(seed.HasValue(), "security seed request failed");
  RequirePositive(seed.Value(), 0x67U);
  Require(seed.Value().payload == std::vector<std::uint8_t>({0x01U, 0x5AU, 0x5AU}),
          "security seed changed");

  auto wrong_key = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kSecurityAccess),
    .payload = {0x02U, 0x00U, 0x00U},
  });
  Require(wrong_key.HasValue(), "wrong security key request failed unexpectedly");
  RequireNegative(
    wrong_key.Value(),
    static_cast<std::uint8_t>(diag::UdsService::kSecurityAccess),
    diag::NegativeResponseCode::kInvalidKey);

  auto correct_key = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kSecurityAccess),
    .payload = {0x02U, 0xFFU, 0xFFU},
  });
  Require(correct_key.HasValue(), "correct security key failed");
  RequirePositive(correct_key.Value(), 0x67U);
  Require(manager.SecurityUnlocked(), "diagnostic security did not unlock");

  Require(manager.ReportDtc({
            .code = 0x0A5001U,
            .status = static_cast<std::uint8_t>(diag::DtcStatus::kTestFailed) |
                      static_cast<std::uint8_t>(diag::DtcStatus::kConfirmed),
            .origin = "ultrasonic-provider",
            .description = "ultrasonic CRC mismatch",
          }).HasValue(),
          "DTC report failed");
  Require(manager.EventMemory().size() == 1U, "DTC event memory size changed");

  auto dtc_count = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDtcInformation),
    .payload = {0x01U, 0xFFU},
  });
  Require(dtc_count.HasValue(), "ReadDTC count failed");
  RequirePositive(dtc_count.Value(), 0x59U);
  Require(dtc_count.Value().payload[3U] == 0x00U, "DTC count high byte changed");
  Require(dtc_count.Value().payload[4U] == 0x01U, "DTC count low byte changed");

  auto dtc_records = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDtcInformation),
    .payload = {0x02U, 0xFFU},
  });
  Require(dtc_records.HasValue(), "ReadDTC records failed");
  RequirePositive(dtc_records.Value(), 0x59U);
  Require(dtc_records.Value().payload.size() == 6U, "DTC record response size changed");
  Require(dtc_records.Value().payload[2U] == 0x0AU, "DTC byte 0 changed");
  Require(dtc_records.Value().payload[3U] == 0x50U, "DTC byte 1 changed");
  Require(dtc_records.Value().payload[4U] == 0x01U, "DTC byte 2 changed");

  Require(manager.RegisterContribution({
            .data_identifiers = {{
              .id = 0xA501U,
              .name = "last-ultrasonic-distance",
              .read = [] { return std::vector<std::uint8_t>{0x05U, 0xDCU}; },
            }},
            .routines = {{
              .id = 0x0201U,
              .name = "ultrasonic-self-test",
              .control = [](diag::RoutineControlType control) {
                return std::vector<std::uint8_t>{static_cast<std::uint8_t>(control), 0x00U};
              },
            }},
          }).HasValue(),
          "diagnostic contribution registration failed");

  auto read_contributed_did = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kReadDataByIdentifier),
    .payload = {0xA5U, 0x01U},
  });
  Require(read_contributed_did.HasValue(), "contributed DID read failed");
  RequirePositive(read_contributed_did.Value(), 0x62U);
  Require(read_contributed_did.Value().payload[2U] == 0x05U, "contributed DID value high changed");
  Require(read_contributed_did.Value().payload[3U] == 0xDCU, "contributed DID value low changed");

  auto routine = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kRoutineControl),
    .payload = {0x01U, 0x02U, 0x01U},
  });
  Require(routine.HasValue(), "routine control failed");
  RequirePositive(routine.Value(), 0x71U);
  Require(routine.Value().payload[3U] == 0x01U, "routine control result changed");

  auto clear_all = manager.HandleRequest({
    .service_id = static_cast<std::uint8_t>(diag::UdsService::kClearDiagnosticInformation),
    .payload = {0xFFU, 0xFFU, 0xFFU},
  });
  Require(clear_all.HasValue(), "clear all DTC request failed");
  RequirePositive(clear_all.Value(), 0x54U);
  Require(manager.EventMemory().empty(), "DTC event memory was not cleared");

  auto can_response = manager.HandleCanRequest(encoded.Value());
  Require(can_response.HasValue(), "diagnostic CAN request handling failed");
  Require(
    can_response.Value().can_id == diag::kDefaultServerResponseCanId,
    "response CAN id changed");

  auto unsupported = manager.HandleRequest({
    .service_id = 0x99U,
    .payload = {},
  });
  Require(unsupported.HasValue(), "unsupported service request failed unexpectedly");
  RequireNegative(unsupported.Value(), 0x99U, diag::NegativeResponseCode::kServiceNotSupported);

  Require(
    diag::ToString(diag::DiagnosticSession::kExtended) == std::string_view("Extended"),
    "diagnostic session text changed");
  Require(
    diag::ToString(diag::NegativeResponseCode::kInvalidKey) == std::string_view("InvalidKey"),
    "diagnostic negative response text changed");

  return 0;
}
