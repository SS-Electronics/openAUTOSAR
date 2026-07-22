// SPDX-License-Identifier: MIT

#include "openautosar/security/crypto_provider.h"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <sstream>
#include <utility>

namespace openautosar::security::crypto {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"cryptography", message};
}

[[nodiscard]] const EVP_MD* MessageDigestFor(Algorithm algorithm) noexcept {
  switch (algorithm) {
    case Algorithm::kSha256:
    case Algorithm::kHmacSha256:
      return EVP_sha256();
  }

  return nullptr;
}

[[nodiscard]] bool Supports(
  const std::vector<Algorithm>& algorithms,
  Algorithm algorithm) {
  return std::find(algorithms.begin(), algorithms.end(), algorithm) != algorithms.end();
}

[[nodiscard]] bool ConstantTimeEquals(
  std::span<const std::uint8_t> left,
  std::span<const std::uint8_t> right) noexcept {
  return left.size() == right.size() &&
         CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

using EvpMdContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using EvpPkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

}  // namespace

ProviderCapabilities OpenSslCryptoProvider::Capabilities() const {
  return {
    .provider_name = "openssl",
    .provider = ProviderKind::kOpenSsl,
    .digest_algorithms = {Algorithm::kSha256},
    .mac_algorithms = {Algorithm::kHmacSha256},
    .key_slots = true,
    .exports_key_material = false,
  };
}

core::Result<bool> OpenSslCryptoProvider::ImportKey(KeyMaterial key) {
  if (key.slot.id.empty() || key.material.empty()) {
    return core::Result<bool>::FromError(MakeError("key slot or material is invalid"));
  }

  if (key.slot.exportable) {
    return core::Result<bool>::FromError(MakeError("exportable key slots are not allowed"));
  }

  if (key.slot.provider != ProviderKind::kOpenSsl) {
    return core::Result<bool>::FromError(MakeError("key slot belongs to another provider"));
  }

  auto iter = std::find_if(
    keys_.begin(),
    keys_.end(),
    [&key](const KeyMaterial& item) { return item.slot.id == key.slot.id; });
  if (iter == keys_.end()) {
    keys_.push_back(std::move(key));
  } else {
    *iter = std::move(key);
  }

  return core::Result<bool>::FromValue(true);
}

core::Result<std::vector<std::uint8_t>> OpenSslCryptoProvider::Digest(
  DigestRequest request) const {
  if (request.algorithm != Algorithm::kSha256) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("digest algorithm is unsupported"));
  }

  const auto* digest = MessageDigestFor(request.algorithm);
  if (digest == nullptr) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("message digest is unavailable"));
  }

  const auto digest_size = EVP_MD_get_size(digest);
  if (digest_size <= 0) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("message digest size is invalid"));
  }

  EvpMdContext context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
  if (!context) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("message digest context allocation failed"));
  }

  if (EVP_DigestInit_ex(context.get(), digest, nullptr) != 1) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("message digest initialization failed"));
  }

  if (!request.payload.empty() &&
      EVP_DigestUpdate(context.get(), request.payload.data(), request.payload.size()) != 1) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("message digest update failed"));
  }

  std::vector<std::uint8_t> result(static_cast<std::size_t>(digest_size));
  unsigned int result_size{0U};
  if (EVP_DigestFinal_ex(context.get(), result.data(), &result_size) != 1) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("message digest finalization failed"));
  }

  result.resize(static_cast<std::size_t>(result_size));
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(result));
}

core::Result<std::vector<std::uint8_t>> OpenSslCryptoProvider::Mac(
  MacRequest request) const {
  if (request.algorithm != Algorithm::kHmacSha256) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("MAC algorithm is unsupported"));
  }

  auto key = FindKey(request.key_slot_id, KeyUsage::kHmac);
  if (!key) {
    return core::Result<std::vector<std::uint8_t>>::FromError(key.Error());
  }

  const auto* digest = MessageDigestFor(request.algorithm);
  if (digest == nullptr) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("HMAC digest is unavailable"));
  }

  EvpPkey pkey{
    EVP_PKEY_new_raw_private_key(
      EVP_PKEY_HMAC,
      nullptr,
      key.Value()->material.data(),
      key.Value()->material.size()),
    EVP_PKEY_free,
  };
  if (!pkey) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("HMAC key allocation failed"));
  }

  EvpMdContext context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
  if (!context) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("HMAC context allocation failed"));
  }

  if (EVP_DigestSignInit(context.get(), nullptr, digest, nullptr, pkey.get()) != 1) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("HMAC initialization failed"));
  }

  if (!request.payload.empty() &&
      EVP_DigestSignUpdate(context.get(), request.payload.data(), request.payload.size()) != 1) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("HMAC update failed"));
  }

  std::size_t mac_size{0U};
  if (EVP_DigestSignFinal(context.get(), nullptr, &mac_size) != 1) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("HMAC size calculation failed"));
  }

  std::vector<std::uint8_t> mac(mac_size);
  if (EVP_DigestSignFinal(context.get(), mac.data(), &mac_size) != 1) {
    return core::Result<std::vector<std::uint8_t>>::FromError(
      MakeError("HMAC finalization failed"));
  }

  mac.resize(mac_size);
  return core::Result<std::vector<std::uint8_t>>::FromValue(std::move(mac));
}

core::Result<bool> OpenSslCryptoProvider::VerifyMac(
  MacVerificationRequest request) const {
  auto calculated = Mac({
    .algorithm = request.algorithm,
    .key_slot_id = request.key_slot_id,
    .payload = std::move(request.payload),
  });
  if (!calculated) {
    return core::Result<bool>::FromError(calculated.Error());
  }

  return core::Result<bool>::FromValue(
    ConstantTimeEquals(calculated.Value(), request.expected_tag));
}

core::Result<const KeyMaterial*> OpenSslCryptoProvider::FindKey(
  std::string_view key_slot_id,
  KeyUsage usage) const {
  auto iter = std::find_if(
    keys_.begin(),
    keys_.end(),
    [key_slot_id](const KeyMaterial& item) { return item.slot.id == key_slot_id; });
  if (iter == keys_.end()) {
    return core::Result<const KeyMaterial*>::FromError(MakeError("key slot is missing"));
  }

  if (iter->slot.usage != usage) {
    return core::Result<const KeyMaterial*>::FromError(MakeError("key usage is not allowed"));
  }

  return core::Result<const KeyMaterial*>::FromValue(&(*iter));
}

core::Result<bool> CryptoManager::RegisterProvider(ICryptoProvider& provider) {
  const auto capabilities = provider.Capabilities();
  if (capabilities.provider_name.empty()) {
    return core::Result<bool>::FromError(MakeError("crypto provider name is empty"));
  }

  const auto duplicate = std::find_if(
    providers_.begin(),
    providers_.end(),
    [&capabilities](const ICryptoProvider* item) {
      return item->Capabilities().provider_name == capabilities.provider_name;
    });
  if (duplicate != providers_.end()) {
    return core::Result<bool>::FromError(MakeError("crypto provider is already registered"));
  }

  providers_.push_back(&provider);
  return core::Result<bool>::FromValue(true);
}

core::Result<bool> CryptoManager::ImportKey(KeyMaterial key) {
  for (auto* provider : providers_) {
    if (provider->Capabilities().provider == key.slot.provider) {
      return provider->ImportKey(std::move(key));
    }
  }

  return core::Result<bool>::FromError(MakeError("crypto provider is missing"));
}

core::Result<std::vector<std::uint8_t>> CryptoManager::Digest(
  DigestRequest request) const {
  auto provider = FindDigestProvider(request.algorithm);
  if (!provider) {
    return core::Result<std::vector<std::uint8_t>>::FromError(provider.Error());
  }

  return provider.Value()->Digest(std::move(request));
}

core::Result<std::vector<std::uint8_t>> CryptoManager::Mac(MacRequest request) const {
  auto provider = FindMacProvider(request.algorithm);
  if (!provider) {
    return core::Result<std::vector<std::uint8_t>>::FromError(provider.Error());
  }

  return provider.Value()->Mac(std::move(request));
}

core::Result<bool> CryptoManager::VerifyMac(MacVerificationRequest request) const {
  auto provider = FindMacProvider(request.algorithm);
  if (!provider) {
    return core::Result<bool>::FromError(provider.Error());
  }

  return provider.Value()->VerifyMac(std::move(request));
}

core::Result<ICryptoProvider*> CryptoManager::FindDigestProvider(Algorithm algorithm) const {
  for (auto* provider : providers_) {
    if (Supports(provider->Capabilities().digest_algorithms, algorithm)) {
      return core::Result<ICryptoProvider*>::FromValue(provider);
    }
  }

  return core::Result<ICryptoProvider*>::FromError(MakeError("digest provider is missing"));
}

core::Result<ICryptoProvider*> CryptoManager::FindMacProvider(Algorithm algorithm) const {
  for (auto* provider : providers_) {
    if (Supports(provider->Capabilities().mac_algorithms, algorithm)) {
      return core::Result<ICryptoProvider*>::FromValue(provider);
    }
  }

  return core::Result<ICryptoProvider*>::FromError(MakeError("MAC provider is missing"));
}

std::string HexEncode(std::span<const std::uint8_t> bytes) {
  std::ostringstream output;
  for (const auto byte : bytes) {
    output << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(byte);
  }
  return output.str();
}

std::string_view ToString(ProviderKind provider) noexcept {
  switch (provider) {
    case ProviderKind::kOpenSsl:
      return "OpenSSL";
    case ProviderKind::kKernel:
      return "Kernel";
    case ProviderKind::kTpm:
      return "TPM";
    case ProviderKind::kHsm:
      return "HSM";
    case ProviderKind::kSecureEnclave:
      return "SecureEnclave";
  }

  return "Unknown";
}

std::string_view ToString(Algorithm algorithm) noexcept {
  switch (algorithm) {
    case Algorithm::kSha256:
      return "SHA-256";
    case Algorithm::kHmacSha256:
      return "HMAC-SHA-256";
  }

  return "Unknown";
}

std::string_view ToString(KeyUsage usage) noexcept {
  switch (usage) {
    case KeyUsage::kHmac:
      return "HMAC";
    case KeyUsage::kSignatureVerification:
      return "SignatureVerification";
    case KeyUsage::kTlsIdentity:
      return "TLSIdentity";
  }

  return "Unknown";
}

}  // namespace openautosar::security::crypto
