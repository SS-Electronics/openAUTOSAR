// SPDX-License-Identifier: MIT

#include "openautosar/virtual_vehicle/ultrasonic_signal_validator.h"

#include <algorithm>
#include <string_view>

namespace openautosar::virtual_vehicle {
namespace {

[[nodiscard]] ValidationFault DecodeFaultFromMessage(std::string_view message) noexcept {
  if (message.find("crc") != std::string_view::npos) {
    return ValidationFault::kCrcMismatch;
  }
  return ValidationFault::kMalformedPdu;
}

[[nodiscard]] std::uint8_t NextAliveCounter(std::uint8_t value) noexcept {
  return static_cast<std::uint8_t>(value + 1U);
}

}  // namespace

UltrasonicSignalValidator::UltrasonicSignalValidator(UltrasonicValidationConfig config)
  : config_(config) {}

UltrasonicValidationReport UltrasonicSignalValidator::ValidateFrame(
  std::span<const std::uint8_t> pdu,
  std::uint64_t now_ns) {
  auto decoded = DecodeClassicUltrasonicPdu(pdu);
  if (!decoded) {
    return BuildFaultReport(DecodeFaultFromMessage(decoded.Error().message));
  }

  const auto& sample = decoded.Value();
  UltrasonicValidationReport report;
  report.last_valid_distance_mm = last_valid_distance_mm_;

  if (now_ns < sample.timestamp_ns || now_ns - sample.timestamp_ns > config_.max_sample_age_ns) {
    report.faults.push_back(ValidationFault::kStaleSample);
  }

  if (previous_alive_counter_.has_value() &&
      sample.alive_counter != NextAliveCounter(previous_alive_counter_.value())) {
    report.faults.push_back(ValidationFault::kAliveCounterJump);
  }

  if (sample.distance_mm < config_.min_distance_mm ||
      sample.distance_mm > config_.max_distance_mm) {
    report.faults.push_back(ValidationFault::kOutOfRange);
  }

  if (sample.quality == UltrasonicQuality::kInvalid) {
    report.faults.push_back(ValidationFault::kInvalidQuality);
  } else if (sample.quality == UltrasonicQuality::kDegraded) {
    report.faults.push_back(ValidationFault::kDegradedQuality);
  }

  previous_alive_counter_ = sample.alive_counter;

  if (!report.faults.empty()) {
    RecordFault(report);
    return report;
  }

  RecordValid(sample, report);
  return report;
}

UltrasonicValidationReport UltrasonicSignalValidator::ValidateSocketCanFrame(
  const canfd_frame& frame,
  std::uint64_t now_ns) {
  auto decoded = DecodeSocketCanUltrasonicFrame(frame);
  if (!decoded) {
    return BuildFaultReport(DecodeFaultFromMessage(decoded.Error().message));
  }

  return ValidateFrame(std::span<const std::uint8_t>(frame.data, frame.len), now_ns);
}

UltrasonicValidationReport UltrasonicSignalValidator::ObserveDroppedFrame() {
  return BuildFaultReport(ValidationFault::kDroppedFrame);
}

UltrasonicValidationReport UltrasonicSignalValidator::BuildFaultReport(ValidationFault fault) {
  UltrasonicValidationReport report;
  report.faults.push_back(fault);
  report.last_valid_distance_mm = last_valid_distance_mm_;
  RecordFault(report);
  return report;
}

void UltrasonicSignalValidator::RecordFault(UltrasonicValidationReport& report) {
  if (consecutive_faults_ < 255U) {
    ++consecutive_faults_;
  }

  report.consecutive_faults = consecutive_faults_;
  report.request_degraded_state = consecutive_faults_ >= config_.degraded_after_consecutive_faults;
  report.state = report.request_degraded_state ? UltrasonicServiceState::kDegraded
                                               : UltrasonicServiceState::kUnavailable;
  report.publish_distance_mm.reset();
  report.last_valid_distance_mm = last_valid_distance_mm_;
}

void UltrasonicSignalValidator::RecordValid(
  const UltrasonicSample& sample,
  UltrasonicValidationReport& report) {
  consecutive_faults_ = 0U;
  last_valid_distance_mm_ = sample.distance_mm;
  report.state = UltrasonicServiceState::kAvailable;
  report.publish_distance_mm = sample.distance_mm;
  report.last_valid_distance_mm = last_valid_distance_mm_;
  report.consecutive_faults = consecutive_faults_;
  report.request_degraded_state = false;
}

bool ContainsFault(const UltrasonicValidationReport& report, ValidationFault fault) {
  return std::find(report.faults.begin(), report.faults.end(), fault) != report.faults.end();
}

const char* ToString(UltrasonicServiceState state) noexcept {
  switch (state) {
    case UltrasonicServiceState::kAvailable:
      return "Available";
    case UltrasonicServiceState::kUnavailable:
      return "Unavailable";
    case UltrasonicServiceState::kDegraded:
      return "Degraded";
  }

  return "Unknown";
}

const char* ToString(ValidationFault fault) noexcept {
  switch (fault) {
    case ValidationFault::kMalformedPdu:
      return "MalformedPdu";
    case ValidationFault::kCrcMismatch:
      return "CrcMismatch";
    case ValidationFault::kStaleSample:
      return "StaleSample";
    case ValidationFault::kAliveCounterJump:
      return "AliveCounterJump";
    case ValidationFault::kOutOfRange:
      return "OutOfRange";
    case ValidationFault::kInvalidQuality:
      return "InvalidQuality";
    case ValidationFault::kDegradedQuality:
      return "DegradedQuality";
    case ValidationFault::kDroppedFrame:
      return "DroppedFrame";
  }

  return "Unknown";
}

}  // namespace openautosar::virtual_vehicle
