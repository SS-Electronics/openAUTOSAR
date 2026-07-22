// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"
#include "openautosar/security/identity_access_manager.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <linux/can.h>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::runtime::diagnostics {

inline constexpr canid_t kDefaultTesterRequestCanId{0x7E0U};
inline constexpr canid_t kDefaultServerResponseCanId{0x7E8U};
inline constexpr std::size_t kMaxSingleFramePayload{63U};

enum class DiagnosticSession : std::uint8_t {
  kDefault = 0x01U,
  kProgramming = 0x02U,
  kExtended = 0x03U,
};

enum class UdsService : std::uint8_t {
  kDiagnosticSessionControl = 0x10U,
  kSecurityAccess = 0x27U,
  kReadDataByIdentifier = 0x22U,
  kReadDtcInformation = 0x19U,
  kClearDiagnosticInformation = 0x14U,
  kRoutineControl = 0x31U,
};

enum class NegativeResponseCode : std::uint8_t {
  kGeneralReject = 0x10U,
  kServiceNotSupported = 0x11U,
  kSubFunctionNotSupported = 0x12U,
  kIncorrectMessageLengthOrInvalidFormat = 0x13U,
  kConditionsNotCorrect = 0x22U,
  kRequestOutOfRange = 0x31U,
  kSecurityAccessDenied = 0x33U,
  kInvalidKey = 0x35U,
};

enum class DtcStatus : std::uint8_t {
  kTestFailed = 0x01U,
  kConfirmed = 0x08U,
  kWarningIndicatorRequested = 0x80U,
};

enum class RoutineControlType : std::uint8_t {
  kStart = 0x01U,
  kStop = 0x02U,
  kRequestResults = 0x03U,
};

struct UdsMessage final {
  std::uint8_t service_id{0U};
  std::vector<std::uint8_t> payload;

  friend bool operator==(const UdsMessage&, const UdsMessage&) = default;
};

struct DiagnosticTransportConfig final {
  canid_t request_can_id{kDefaultTesterRequestCanId};
  canid_t response_can_id{kDefaultServerResponseCanId};
};

struct DataIdentifier final {
  std::uint16_t id{0U};
  std::string name;
  std::function<std::vector<std::uint8_t>()> read;
};

struct DtcRecord final {
  std::uint32_t code{0U};
  std::uint8_t status{0U};
  std::string origin;
  std::string description;
};

struct Routine final {
  std::uint16_t id{0U};
  std::string name;
  std::function<std::vector<std::uint8_t>(RoutineControlType)> control;
};

struct DiagnosticContribution final {
  std::vector<DataIdentifier> data_identifiers;
  std::vector<Routine> routines;
};

class UdsCanFrameCodec final {
public:
  [[nodiscard]] static core::Result<canfd_frame> EncodeSingleFrame(
    const UdsMessage& message,
    canid_t can_id);
  [[nodiscard]] static core::Result<UdsMessage> DecodeSingleFrame(
    const canfd_frame& frame,
    canid_t expected_can_id);
};

class DiagnosticManager final {
public:
  explicit DiagnosticManager(DiagnosticTransportConfig transport = {});

  [[nodiscard]] core::Result<bool> RegisterDataIdentifier(DataIdentifier identifier);
  [[nodiscard]] core::Result<bool> RegisterRoutine(Routine routine);
  [[nodiscard]] core::Result<bool> RegisterContribution(DiagnosticContribution contribution);

  [[nodiscard]] core::Result<bool> ReportDtc(DtcRecord record);
  [[nodiscard]] core::Result<bool> ClearDtc(std::uint32_t code);

  [[nodiscard]] core::Result<UdsMessage> HandleRequest(const UdsMessage& request);
  [[nodiscard]] core::Result<canfd_frame> HandleCanRequest(const canfd_frame& frame);

  void SetAuthorizationPolicy(
    const security::AccessPolicyEngine& authorization_policy,
    security::Principal tester_principal);
  void ClearAuthorizationPolicy() noexcept;
  void SetSecurityEventCollector(security::SecurityEventCollector& collector) noexcept;
  void ClearSecurityEventCollector() noexcept;

  [[nodiscard]] DiagnosticSession Session() const noexcept { return session_; }
  [[nodiscard]] bool SecurityUnlocked() const noexcept { return security_unlocked_; }
  [[nodiscard]] std::vector<DtcRecord> EventMemory() const;

private:
  [[nodiscard]] UdsMessage HandleSessionControl(const UdsMessage& request);
  [[nodiscard]] UdsMessage HandleSecurityAccess(const UdsMessage& request);
  [[nodiscard]] UdsMessage HandleReadDataByIdentifier(const UdsMessage& request);
  [[nodiscard]] UdsMessage HandleReadDtcInformation(const UdsMessage& request) const;
  [[nodiscard]] UdsMessage HandleClearDiagnosticInformation(const UdsMessage& request);
  [[nodiscard]] UdsMessage HandleRoutineControl(const UdsMessage& request);

  [[nodiscard]] UdsMessage Negative(
    std::uint8_t request_sid,
    NegativeResponseCode code) const;
  [[nodiscard]] core::Result<bool> AuthorizeDiagnosticAccess(
    security::Operation operation,
    std::string_view resource) const;
  [[nodiscard]] static std::uint16_t ReadU16(const std::vector<std::uint8_t>& payload);
  [[nodiscard]] static std::vector<std::uint8_t> U16(std::uint16_t value);
  [[nodiscard]] static std::vector<std::uint8_t> DtcBytes(std::uint32_t code);

  DiagnosticTransportConfig transport_{};
  const security::AccessPolicyEngine* authorization_policy_{nullptr};
  security::SecurityEventCollector* security_events_{nullptr};
  security::Principal tester_principal_{};
  DiagnosticSession session_{DiagnosticSession::kDefault};
  bool security_unlocked_{false};
  std::uint16_t pending_seed_{0U};
  std::map<std::uint16_t, DataIdentifier> data_identifiers_;
  std::map<std::uint16_t, Routine> routines_;
  std::map<std::uint32_t, DtcRecord> event_memory_;
};

[[nodiscard]] std::string_view ToString(DiagnosticSession session) noexcept;
[[nodiscard]] std::string_view ToString(NegativeResponseCode code) noexcept;

}  // namespace openautosar::runtime::diagnostics
