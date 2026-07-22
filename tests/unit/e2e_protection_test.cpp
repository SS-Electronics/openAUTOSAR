// SPDX-License-Identifier: MIT

#include "openautosar/e2e/e2e_protection.h"

#include <cstdlib>
#include <iostream>
#include <string>
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

std::vector<std::uint8_t> Bytes(std::string_view text) {
  std::vector<std::uint8_t> result;
  result.reserve(text.size());
  for (const char item : text) {
    result.push_back(static_cast<std::uint8_t>(item));
  }
  return result;
}

openautosar::e2e::ProfileConfig BaseConfig() {
  return {
    .profile = openautosar::e2e::Profile::kProfile01,
    .data_id = 0xA5018001U,
    .counter_bits = 8U,
    .max_delta_counter = 1U,
    .timeout_ms = 100U,
    .max_repetitions = 0U,
    .trace = {
      .service_instance = "/OpenAUTOSAR/Vehicle/Ultrasonic/FrontCenter",
      .event_name = "DistanceSample",
      .source_model_path = "model/examples/vehicle/ultrasonic_service.json",
      .source_model_pointer = "/services/0/events/0/e2e",
      .deployment_ref = "openautosar.communication.e2e.profile01",
    },
  };
}

}  // namespace

int main() {
  namespace e2e = openautosar::e2e;

  auto invalid = BaseConfig();
  invalid.data_id = 0U;
  Require(!e2e::ValidateProfileConfig(invalid).HasValue(), "zero data ID was accepted");

  const auto crc_input = Bytes("123456789");
  Require(
    e2e::CalculateCrc32(crc_input) == 0xCBF43926U,
    "CRC-32/ISO-HDLC golden vector changed");

  const auto config = BaseConfig();
  Require(e2e::ValidateProfileConfig(config).HasValue(), "valid E2E config rejected");

  const auto next_counter = e2e::NextCounter(config, 255U);
  Require(next_counter.HasValue() && next_counter.Value() == 0U, "8-bit counter did not wrap");

  const std::vector<std::uint8_t> payload{0x01U, 0x2CU, 0x62U, 0x01U};
  const auto protected_payload = e2e::ProtectPayload(config, payload, 1U, 1'000U);
  Require(protected_payload.HasValue(), "payload protection failed");
  Require(
    protected_payload.Value().frame.size() ==
      e2e::kProtectedFrameHeaderSize + payload.size(),
    "protected frame size changed");
  Require(
    protected_payload.Value().header.data_id == config.data_id,
    "protected frame data ID changed");
  Require(
    protected_payload.Value().trace.source_model_pointer == "/services/0/events/0/e2e",
    "E2E trace pointer changed");

  e2e::ReceiverState state;
  const auto checked = e2e::CheckPayload(config, protected_payload.Value().frame, 1'010U, &state);
  Require(checked.HasValue(), "payload check failed");
  Require(checked.Value().status == e2e::CheckStatus::kOk, "valid E2E payload rejected");
  Require(checked.Value().accepted, "valid E2E payload was not accepted");
  Require(checked.Value().payload == payload, "checked E2E payload changed");
  Require(state.initialized && state.last_counter == 1U, "receiver state did not initialize");

  const auto repeated = e2e::CheckPayload(config, protected_payload.Value().frame, 1'011U, &state);
  Require(repeated.HasValue(), "repeated E2E check failed");
  Require(
    repeated.Value().status == e2e::CheckStatus::kRepeated,
    "repeated E2E counter not detected");
  Require(!repeated.Value().accepted, "repeated E2E payload was accepted unexpectedly");

  const auto sequence_jump = e2e::ProtectPayload(config, payload, 5U, 1'012U);
  Require(sequence_jump.HasValue(), "sequence jump frame protection failed");
  const auto sequence_status =
    e2e::CheckPayload(config, sequence_jump.Value().frame, 1'013U, &state);
  Require(sequence_status.HasValue(), "sequence jump check failed");
  Require(
    sequence_status.Value().status == e2e::CheckStatus::kWrongSequence,
    "E2E sequence jump not detected");

  const auto stale = e2e::ProtectPayload(
    config,
    payload,
    2U,
    1'020U,
    e2e::FaultInjectionMode::kStaleTimestamp);
  Require(stale.HasValue(), "stale timestamp injection failed");
  Require(stale.Value().status == e2e::CheckStatus::kFaultInjected, "fault status changed");
  const auto stale_status = e2e::CheckPayload(config, stale.Value().frame, 1'020U, &state);
  Require(stale_status.HasValue(), "stale frame check failed");
  Require(
    stale_status.Value().status == e2e::CheckStatus::kTimeout,
    "stale E2E frame was not timed out");

  const auto wrong_data_id = e2e::ProtectPayload(
    config,
    payload,
    2U,
    1'030U,
    e2e::FaultInjectionMode::kWrongDataId);
  Require(wrong_data_id.HasValue(), "wrong data ID injection failed");
  const auto wrong_data_id_status =
    e2e::CheckPayload(config, wrong_data_id.Value().frame, 1'031U, &state);
  Require(wrong_data_id_status.HasValue(), "wrong data ID check failed");
  Require(
    wrong_data_id_status.Value().status == e2e::CheckStatus::kWrongDataId,
    "wrong E2E data ID not detected");

  const auto corrupt_crc = e2e::ProtectPayload(
    config,
    payload,
    2U,
    1'040U,
    e2e::FaultInjectionMode::kCorruptCrc);
  Require(corrupt_crc.HasValue(), "CRC fault injection failed");
  const auto corrupt_crc_status =
    e2e::CheckPayload(config, corrupt_crc.Value().frame, 1'041U, &state);
  Require(corrupt_crc_status.HasValue(), "CRC fault check failed");
  Require(
    corrupt_crc_status.Value().status == e2e::CheckStatus::kCrcMismatch,
    "corrupt E2E CRC not detected");

  const auto truncated = e2e::ProtectPayload(
    config,
    payload,
    2U,
    1'050U,
    e2e::FaultInjectionMode::kTruncatePayload);
  Require(truncated.HasValue(), "truncate fault injection failed");
  const auto malformed = e2e::CheckPayload(config, truncated.Value().frame, 1'051U, &state);
  Require(malformed.HasValue(), "malformed E2E check failed");
  Require(
    malformed.Value().status == e2e::CheckStatus::kMalformed,
    "truncated E2E frame not detected");

  auto wrap_config = config;
  wrap_config.counter_bits = 4U;
  wrap_config.max_delta_counter = 1U;
  e2e::ReceiverState wrap_state{
    .initialized = true,
    .last_counter = 15U,
    .last_timestamp_ms = 2'000U,
  };
  const auto wrapped = e2e::ProtectPayload(wrap_config, payload, 0U, 2'001U);
  Require(wrapped.HasValue(), "wrapped counter protection failed");
  const auto wrapped_status =
    e2e::CheckPayload(wrap_config, wrapped.Value().frame, 2'002U, &wrap_state);
  Require(wrapped_status.HasValue(), "wrapped counter check failed");
  Require(
    wrapped_status.Value().status == e2e::CheckStatus::kOk,
    "wrapped E2E counter rejected");

  Require(
    e2e::ToString(e2e::Profile::kProfile01) == std::string_view("Profile01"),
    "profile text changed");
  Require(
    e2e::ToString(e2e::CheckStatus::kCrcMismatch) == std::string_view("CrcMismatch"),
    "status text changed");
  Require(
    e2e::ToString(e2e::FaultInjectionMode::kTruncatePayload) ==
      std::string_view("TruncatePayload"),
    "fault-injection text changed");

  return 0;
}
