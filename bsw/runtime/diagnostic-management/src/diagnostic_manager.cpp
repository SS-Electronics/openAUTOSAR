// SPDX-License-Identifier: MIT

#include "openautosar/runtime/diagnostic_manager.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace openautosar::runtime::diagnostics {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"diagnostic-management", message};
}

[[nodiscard]] std::uint8_t PositiveResponseSid(std::uint8_t service_id) noexcept {
  return static_cast<std::uint8_t>(service_id + 0x40U);
}

[[nodiscard]] bool IsKnownSession(std::uint8_t value) noexcept {
  return value == static_cast<std::uint8_t>(DiagnosticSession::kDefault) ||
         value == static_cast<std::uint8_t>(DiagnosticSession::kProgramming) ||
         value == static_cast<std::uint8_t>(DiagnosticSession::kExtended);
}

[[nodiscard]] std::uint8_t DtcStatusMask(const DtcRecord& record) noexcept {
  return record.status;
}

}  // namespace

core::Result<canfd_frame> UdsCanFrameCodec::EncodeSingleFrame(
  const UdsMessage& message,
  canid_t can_id) {
  if (message.service_id == 0U) {
    return core::Result<canfd_frame>::FromError(MakeError("UDS service id is zero"));
  }

  if (message.payload.size() + 1U > kMaxSingleFramePayload) {
    return core::Result<canfd_frame>::FromError(
      MakeError("UDS payload exceeds single-frame limit"));
  }

  canfd_frame frame{};
  frame.can_id = can_id;
  frame.len = static_cast<__u8>(message.payload.size() + 2U);
  frame.data[0U] = static_cast<std::uint8_t>(message.payload.size() + 1U);
  frame.data[1U] = message.service_id;
  if (!message.payload.empty()) {
    std::memcpy(&frame.data[2U], message.payload.data(), message.payload.size());
  }

  return core::Result<canfd_frame>::FromValue(frame);
}

core::Result<UdsMessage> UdsCanFrameCodec::DecodeSingleFrame(
  const canfd_frame& frame,
  canid_t expected_can_id) {
  if (frame.can_id != expected_can_id) {
    return core::Result<UdsMessage>::FromError(MakeError("CAN id does not match diagnostic route"));
  }

  if (frame.len < 2U || frame.len > CANFD_MAX_DLEN) {
    return core::Result<UdsMessage>::FromError(MakeError("CAN diagnostic frame length is invalid"));
  }

  const auto uds_length = frame.data[0U];
  if (uds_length == 0U || uds_length + 1U != frame.len) {
    return core::Result<UdsMessage>::FromError(MakeError("ISO-TP single-frame length mismatch"));
  }

  UdsMessage message{
    .service_id = frame.data[1U],
    .payload = {},
  };
  message.payload.assign(frame.data + 2U, frame.data + frame.len);
  return core::Result<UdsMessage>::FromValue(std::move(message));
}

DiagnosticManager::DiagnosticManager(DiagnosticTransportConfig transport)
  : transport_(transport), pending_seed_(0x5A5AU) {
  static_cast<void>(RegisterDataIdentifier({
    .id = 0xF180U,
    .name = "boot-software-id",
    .read = [] { return std::vector<std::uint8_t>{'o', 'p', 'e', 'n', 'A', 'U', 'T', 'O'}; },
  }));
  static_cast<void>(RegisterDataIdentifier({
    .id = 0xF189U,
    .name = "software-version",
    .read = [] { return std::vector<std::uint8_t>{'0', '.', '1', '.', '0'}; },
  }));
}

core::Result<bool> DiagnosticManager::RegisterDataIdentifier(DataIdentifier identifier) {
  if (identifier.id == 0U || identifier.name.empty() || !identifier.read) {
    return core::Result<bool>::FromError(MakeError("data identifier is invalid"));
  }

  if (data_identifiers_.find(identifier.id) != data_identifiers_.end()) {
    return core::Result<bool>::FromError(MakeError("data identifier is already registered"));
  }

  data_identifiers_.emplace(identifier.id, std::move(identifier));
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> DiagnosticManager::RegisterRoutine(Routine routine) {
  if (routine.id == 0U || routine.name.empty() || !routine.control) {
    return core::Result<bool>::FromError(MakeError("diagnostic routine is invalid"));
  }

  if (routines_.find(routine.id) != routines_.end()) {
    return core::Result<bool>::FromError(MakeError("diagnostic routine is already registered"));
  }

  routines_.emplace(routine.id, std::move(routine));
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> DiagnosticManager::RegisterContribution(DiagnosticContribution contribution) {
  for (auto& identifier : contribution.data_identifiers) {
    auto registered = RegisterDataIdentifier(std::move(identifier));
    if (!registered) {
      return core::Result<bool>::FromError(registered.Error());
    }
  }

  for (auto& routine : contribution.routines) {
    auto registered = RegisterRoutine(std::move(routine));
    if (!registered) {
      return core::Result<bool>::FromError(registered.Error());
    }
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<bool> DiagnosticManager::ReportDtc(DtcRecord record) {
  if (record.code == 0U || record.code > 0x00FFFFFFU || record.description.empty()) {
    return core::Result<bool>::FromError(MakeError("DTC record is invalid"));
  }

  event_memory_[record.code] = std::move(record);
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> DiagnosticManager::ClearDtc(std::uint32_t code) {
  if (code > 0x00FFFFFFU) {
    return core::Result<bool>::FromError(MakeError("DTC code is outside 24-bit range"));
  }

  if (code == 0x00FFFFFFU) {
    event_memory_.clear();
    return core::Result<bool>::FromValue(true);
  }

  const auto erased = event_memory_.erase(code);
  if (erased == 0U) {
    return core::Result<bool>::FromError(MakeError("DTC record is missing"));
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<UdsMessage> DiagnosticManager::HandleRequest(const UdsMessage& request) {
  if (request.service_id == 0U) {
    return core::Result<UdsMessage>::FromError(MakeError("UDS request service id is zero"));
  }

  switch (static_cast<UdsService>(request.service_id)) {
    case UdsService::kDiagnosticSessionControl:
      return core::Result<UdsMessage>::FromValue(HandleSessionControl(request));
    case UdsService::kSecurityAccess:
      return core::Result<UdsMessage>::FromValue(HandleSecurityAccess(request));
    case UdsService::kReadDataByIdentifier:
      return core::Result<UdsMessage>::FromValue(HandleReadDataByIdentifier(request));
    case UdsService::kReadDtcInformation:
      return core::Result<UdsMessage>::FromValue(HandleReadDtcInformation(request));
    case UdsService::kClearDiagnosticInformation:
      return core::Result<UdsMessage>::FromValue(HandleClearDiagnosticInformation(request));
    case UdsService::kRoutineControl:
      return core::Result<UdsMessage>::FromValue(HandleRoutineControl(request));
  }

  return core::Result<UdsMessage>::FromValue(
    Negative(request.service_id, NegativeResponseCode::kServiceNotSupported));
}

core::Result<canfd_frame> DiagnosticManager::HandleCanRequest(const canfd_frame& frame) {
  auto request = UdsCanFrameCodec::DecodeSingleFrame(frame, transport_.request_can_id);
  if (!request) {
    return core::Result<canfd_frame>::FromError(request.Error());
  }

  auto response = HandleRequest(request.Value());
  if (!response) {
    return core::Result<canfd_frame>::FromError(response.Error());
  }

  return UdsCanFrameCodec::EncodeSingleFrame(response.Value(), transport_.response_can_id);
}

void DiagnosticManager::SetAuthorizationPolicy(
  const security::AccessPolicyEngine& authorization_policy,
  security::Principal tester_principal) {
  authorization_policy_ = &authorization_policy;
  tester_principal_ = std::move(tester_principal);
}

void DiagnosticManager::ClearAuthorizationPolicy() noexcept {
  authorization_policy_ = nullptr;
  tester_principal_ = {};
}

void DiagnosticManager::SetSecurityEventCollector(
  security::SecurityEventCollector& collector) noexcept {
  security_events_ = &collector;
}

void DiagnosticManager::ClearSecurityEventCollector() noexcept {
  security_events_ = nullptr;
}

std::vector<DtcRecord> DiagnosticManager::EventMemory() const {
  std::vector<DtcRecord> records;
  records.reserve(event_memory_.size());
  for (const auto& [_, record] : event_memory_) {
    records.push_back(record);
  }
  return records;
}

UdsMessage DiagnosticManager::HandleSessionControl(const UdsMessage& request) {
  if (request.payload.size() != 1U) {
    return Negative(
      request.service_id,
      NegativeResponseCode::kIncorrectMessageLengthOrInvalidFormat);
  }

  if (!IsKnownSession(request.payload[0U])) {
    return Negative(request.service_id, NegativeResponseCode::kSubFunctionNotSupported);
  }

  session_ = static_cast<DiagnosticSession>(request.payload[0U]);
  security_unlocked_ = false;
  return {
    .service_id = PositiveResponseSid(request.service_id),
    .payload = {request.payload[0U], 0x00U, 0x32U, 0x01U, 0xF4U},
  };
}

UdsMessage DiagnosticManager::HandleSecurityAccess(const UdsMessage& request) {
  if (request.payload.empty()) {
    return Negative(
      request.service_id,
      NegativeResponseCode::kIncorrectMessageLengthOrInvalidFormat);
  }

  const auto subfunction = request.payload[0U];
  if (subfunction == 0x01U) {
    return {
      .service_id = PositiveResponseSid(request.service_id),
      .payload = {subfunction, 0x5AU, 0x5AU},
    };
  }

  if (subfunction == 0x02U) {
    if (request.payload.size() != 3U) {
      return Negative(
        request.service_id,
        NegativeResponseCode::kIncorrectMessageLengthOrInvalidFormat);
    }

    const auto key = ReadU16({request.payload[1U], request.payload[2U]});
    const auto expected_key = static_cast<std::uint16_t>(pending_seed_ ^ 0xA5A5U);
    if (key != expected_key) {
      return Negative(request.service_id, NegativeResponseCode::kInvalidKey);
    }

    auto authorized = AuthorizeDiagnosticAccess(
      security::Operation::kDiagnosticSecurityAccess,
      "uds/security-access");
    if (!authorized) {
      return Negative(request.service_id, NegativeResponseCode::kSecurityAccessDenied);
    }

    security_unlocked_ = true;
    return {
      .service_id = PositiveResponseSid(request.service_id),
      .payload = {subfunction},
    };
  }

  return Negative(request.service_id, NegativeResponseCode::kSubFunctionNotSupported);
}

UdsMessage DiagnosticManager::HandleReadDataByIdentifier(const UdsMessage& request) {
  if (request.payload.empty() || request.payload.size() % 2U != 0U) {
    return Negative(
      request.service_id,
      NegativeResponseCode::kIncorrectMessageLengthOrInvalidFormat);
  }

  auto authorized = AuthorizeDiagnosticAccess(
    security::Operation::kDiagnosticRead,
    "uds/read-data-by-identifier");
  if (!authorized) {
    return Negative(request.service_id, NegativeResponseCode::kSecurityAccessDenied);
  }

  std::vector<std::uint8_t> payload;
  for (std::size_t offset = 0U; offset < request.payload.size(); offset += 2U) {
    const auto did = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(request.payload[offset]) << 8U |
      static_cast<std::uint16_t>(request.payload[offset + 1U]));
    const auto iter = data_identifiers_.find(did);
    if (iter == data_identifiers_.end()) {
      return Negative(request.service_id, NegativeResponseCode::kRequestOutOfRange);
    }

    auto did_bytes = U16(did);
    auto value = iter->second.read();
    payload.insert(payload.end(), did_bytes.begin(), did_bytes.end());
    payload.insert(payload.end(), value.begin(), value.end());
  }

  return {
    .service_id = PositiveResponseSid(request.service_id),
    .payload = std::move(payload),
  };
}

UdsMessage DiagnosticManager::HandleReadDtcInformation(const UdsMessage& request) const {
  if (request.payload.empty()) {
    return Negative(
      request.service_id,
      NegativeResponseCode::kIncorrectMessageLengthOrInvalidFormat);
  }

  auto authorized = AuthorizeDiagnosticAccess(
    security::Operation::kDiagnosticRead,
    "uds/read-dtc-information");
  if (!authorized) {
    return Negative(request.service_id, NegativeResponseCode::kSecurityAccessDenied);
  }

  const auto subfunction = request.payload[0U];
  if (subfunction == 0x01U) {
    const std::uint8_t status_mask = request.payload.size() > 1U ? request.payload[1U] : 0xFFU;
    std::uint16_t count{0U};
    for (const auto& [_, record] : event_memory_) {
      if ((DtcStatusMask(record) & status_mask) != 0U) {
        ++count;
      }
    }
    auto count_bytes = U16(count);
    return {
      .service_id = PositiveResponseSid(request.service_id),
      .payload = {subfunction, status_mask, 0x00U, count_bytes[0U], count_bytes[1U]},
    };
  }

  if (subfunction == 0x02U) {
    const std::uint8_t status_mask = request.payload.size() > 1U ? request.payload[1U] : 0xFFU;
    std::vector<std::uint8_t> payload{subfunction, status_mask};
    for (const auto& [_, record] : event_memory_) {
      if ((DtcStatusMask(record) & status_mask) == 0U) {
        continue;
      }

      auto dtc = DtcBytes(record.code);
      payload.insert(payload.end(), dtc.begin(), dtc.end());
      payload.push_back(record.status);
    }
    return {
      .service_id = PositiveResponseSid(request.service_id),
      .payload = std::move(payload),
    };
  }

  return Negative(request.service_id, NegativeResponseCode::kSubFunctionNotSupported);
}

UdsMessage DiagnosticManager::HandleClearDiagnosticInformation(const UdsMessage& request) {
  if (!security_unlocked_) {
    return Negative(request.service_id, NegativeResponseCode::kSecurityAccessDenied);
  }

  auto authorized = AuthorizeDiagnosticAccess(
    security::Operation::kDiagnosticClearDtc,
    "uds/clear-diagnostic-information");
  if (!authorized) {
    return Negative(request.service_id, NegativeResponseCode::kSecurityAccessDenied);
  }

  if (request.payload.size() != 3U) {
    return Negative(
      request.service_id,
      NegativeResponseCode::kIncorrectMessageLengthOrInvalidFormat);
  }

  const auto code = static_cast<std::uint32_t>(
    static_cast<std::uint32_t>(request.payload[0U]) << 16U |
    static_cast<std::uint32_t>(request.payload[1U]) << 8U |
    static_cast<std::uint32_t>(request.payload[2U]));
  auto cleared = ClearDtc(code);
  if (!cleared) {
    return Negative(request.service_id, NegativeResponseCode::kRequestOutOfRange);
  }

  return {
    .service_id = PositiveResponseSid(request.service_id),
    .payload = {},
  };
}

UdsMessage DiagnosticManager::HandleRoutineControl(const UdsMessage& request) {
  if (session_ == DiagnosticSession::kDefault) {
    return Negative(request.service_id, NegativeResponseCode::kConditionsNotCorrect);
  }

  if (!security_unlocked_) {
    return Negative(request.service_id, NegativeResponseCode::kSecurityAccessDenied);
  }

  auto authorized = AuthorizeDiagnosticAccess(
    security::Operation::kDiagnosticRoutineControl,
    "uds/routine-control");
  if (!authorized) {
    return Negative(request.service_id, NegativeResponseCode::kSecurityAccessDenied);
  }

  if (request.payload.size() < 3U) {
    return Negative(
      request.service_id,
      NegativeResponseCode::kIncorrectMessageLengthOrInvalidFormat);
  }

  const auto control_type = request.payload[0U];
  if (control_type < static_cast<std::uint8_t>(RoutineControlType::kStart) ||
      control_type > static_cast<std::uint8_t>(RoutineControlType::kRequestResults)) {
    return Negative(request.service_id, NegativeResponseCode::kSubFunctionNotSupported);
  }

  const auto routine_id = static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(request.payload[1U]) << 8U |
    static_cast<std::uint16_t>(request.payload[2U]));
  const auto iter = routines_.find(routine_id);
  if (iter == routines_.end()) {
    return Negative(request.service_id, NegativeResponseCode::kRequestOutOfRange);
  }

  std::vector<std::uint8_t> payload{control_type, request.payload[1U], request.payload[2U]};
  auto result = iter->second.control(static_cast<RoutineControlType>(control_type));
  payload.insert(payload.end(), result.begin(), result.end());
  return {
    .service_id = PositiveResponseSid(request.service_id),
    .payload = std::move(payload),
  };
}

UdsMessage DiagnosticManager::Negative(
  std::uint8_t request_sid,
  NegativeResponseCode code) const {
  return {
    .service_id = 0x7FU,
    .payload = {request_sid, static_cast<std::uint8_t>(code)},
  };
}

core::Result<bool> DiagnosticManager::AuthorizeDiagnosticAccess(
  security::Operation operation,
  std::string_view resource) const {
  if (authorization_policy_ == nullptr) {
    return core::Result<bool>::FromValue(true);
  }

  const auto decision = authorization_policy_->Authorize({
    .principal = tester_principal_,
    .resource = {
      .kind = security::ResourceKind::kDiagnostic,
      .identifier = std::string(resource),
      .policy_id = "uds",
    },
    .operation = operation,
    .action = std::string(security::ToString(operation)),
  });
  if (!decision.Allowed()) {
    if (security_events_ != nullptr) {
      static_cast<void>(security_events_->Record({
        .source = "diagnostics",
        .category = "authorization",
        .severity = decision.severity,
        .principal_id = tester_principal_.application_id,
        .resource_id = std::string(resource),
        .operation = operation,
        .detail = decision.reason,
      }));
    }
    return core::Result<bool>::FromError(MakeError(decision.reason.c_str()));
  }

  return core::Result<bool>::FromValue(true);
}

std::uint16_t DiagnosticManager::ReadU16(const std::vector<std::uint8_t>& payload) {
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(payload[0U]) << 8U |
    static_cast<std::uint16_t>(payload[1U]));
}

std::vector<std::uint8_t> DiagnosticManager::U16(std::uint16_t value) {
  return {
    static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
    static_cast<std::uint8_t>(value & 0xFFU),
  };
}

std::vector<std::uint8_t> DiagnosticManager::DtcBytes(std::uint32_t code) {
  return {
    static_cast<std::uint8_t>((code >> 16U) & 0xFFU),
    static_cast<std::uint8_t>((code >> 8U) & 0xFFU),
    static_cast<std::uint8_t>(code & 0xFFU),
  };
}

std::string_view ToString(DiagnosticSession session) noexcept {
  switch (session) {
    case DiagnosticSession::kDefault:
      return "Default";
    case DiagnosticSession::kProgramming:
      return "Programming";
    case DiagnosticSession::kExtended:
      return "Extended";
  }

  return "Unknown";
}

std::string_view ToString(NegativeResponseCode code) noexcept {
  switch (code) {
    case NegativeResponseCode::kGeneralReject:
      return "GeneralReject";
    case NegativeResponseCode::kServiceNotSupported:
      return "ServiceNotSupported";
    case NegativeResponseCode::kSubFunctionNotSupported:
      return "SubFunctionNotSupported";
    case NegativeResponseCode::kIncorrectMessageLengthOrInvalidFormat:
      return "IncorrectMessageLengthOrInvalidFormat";
    case NegativeResponseCode::kConditionsNotCorrect:
      return "ConditionsNotCorrect";
    case NegativeResponseCode::kRequestOutOfRange:
      return "RequestOutOfRange";
    case NegativeResponseCode::kSecurityAccessDenied:
      return "SecurityAccessDenied";
    case NegativeResponseCode::kInvalidKey:
      return "InvalidKey";
  }

  return "Unknown";
}

}  // namespace openautosar::runtime::diagnostics
