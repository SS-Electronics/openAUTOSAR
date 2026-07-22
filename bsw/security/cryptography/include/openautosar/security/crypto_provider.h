// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::security::crypto {

enum class ProviderKind {
  kOpenSsl,
  kKernel,
  kTpm,
  kHsm,
  kSecureEnclave,
};

enum class Algorithm {
  kSha256,
  kHmacSha256,
};

enum class KeyUsage {
  kHmac,
  kSignatureVerification,
  kTlsIdentity,
};

struct KeySlot final {
  std::string id;
  ProviderKind provider{ProviderKind::kOpenSsl};
  KeyUsage usage{KeyUsage::kHmac};
  bool exportable{false};
};

struct KeyMaterial final {
  KeySlot slot;
  std::vector<std::uint8_t> material;
};

struct DigestRequest final {
  Algorithm algorithm{Algorithm::kSha256};
  std::vector<std::uint8_t> payload;
};

struct MacRequest final {
  Algorithm algorithm{Algorithm::kHmacSha256};
  std::string key_slot_id;
  std::vector<std::uint8_t> payload;
};

struct MacVerificationRequest final {
  Algorithm algorithm{Algorithm::kHmacSha256};
  std::string key_slot_id;
  std::vector<std::uint8_t> payload;
  std::vector<std::uint8_t> expected_tag;
};

struct ProviderCapabilities final {
  std::string provider_name;
  ProviderKind provider{ProviderKind::kOpenSsl};
  std::vector<Algorithm> digest_algorithms;
  std::vector<Algorithm> mac_algorithms;
  bool key_slots{true};
  bool exports_key_material{false};
};

class ICryptoProvider {
public:
  ICryptoProvider() = default;
  ICryptoProvider(const ICryptoProvider&) = delete;
  ICryptoProvider& operator=(const ICryptoProvider&) = delete;
  virtual ~ICryptoProvider() = default;

  [[nodiscard]] virtual ProviderCapabilities Capabilities() const = 0;
  [[nodiscard]] virtual core::Result<bool> ImportKey(KeyMaterial key) = 0;
  [[nodiscard]] virtual core::Result<std::vector<std::uint8_t>> Digest(
    DigestRequest request) const = 0;
  [[nodiscard]] virtual core::Result<std::vector<std::uint8_t>> Mac(
    MacRequest request) const = 0;
  [[nodiscard]] virtual core::Result<bool> VerifyMac(
    MacVerificationRequest request) const = 0;
};

class OpenSslCryptoProvider final : public ICryptoProvider {
public:
  [[nodiscard]] ProviderCapabilities Capabilities() const override;
  [[nodiscard]] core::Result<bool> ImportKey(KeyMaterial key) override;
  [[nodiscard]] core::Result<std::vector<std::uint8_t>> Digest(
    DigestRequest request) const override;
  [[nodiscard]] core::Result<std::vector<std::uint8_t>> Mac(
    MacRequest request) const override;
  [[nodiscard]] core::Result<bool> VerifyMac(
    MacVerificationRequest request) const override;

private:
  [[nodiscard]] core::Result<const KeyMaterial*> FindKey(
    std::string_view key_slot_id,
    KeyUsage usage) const;

  std::vector<KeyMaterial> keys_;
};

class CryptoManager final {
public:
  [[nodiscard]] core::Result<bool> RegisterProvider(ICryptoProvider& provider);
  [[nodiscard]] core::Result<bool> ImportKey(KeyMaterial key);
  [[nodiscard]] core::Result<std::vector<std::uint8_t>> Digest(
    DigestRequest request) const;
  [[nodiscard]] core::Result<std::vector<std::uint8_t>> Mac(MacRequest request) const;
  [[nodiscard]] core::Result<bool> VerifyMac(MacVerificationRequest request) const;

private:
  [[nodiscard]] core::Result<ICryptoProvider*> FindDigestProvider(
    Algorithm algorithm) const;
  [[nodiscard]] core::Result<ICryptoProvider*> FindMacProvider(
    Algorithm algorithm) const;

  std::vector<ICryptoProvider*> providers_;
};

[[nodiscard]] std::string HexEncode(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string_view ToString(ProviderKind provider) noexcept;
[[nodiscard]] std::string_view ToString(Algorithm algorithm) noexcept;
[[nodiscard]] std::string_view ToString(KeyUsage usage) noexcept;

}  // namespace openautosar::security::crypto
