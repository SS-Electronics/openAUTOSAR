// SPDX-License-Identifier: MIT

#include "openautosar/security/crypto_provider.h"

#include <cstdint>
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
  std::vector<std::uint8_t> bytes;
  bytes.reserve(text.size());
  for (const char item : text) {
    bytes.push_back(static_cast<std::uint8_t>(item));
  }
  return bytes;
}

}  // namespace

int main() {
  namespace crypto = openautosar::security::crypto;

  crypto::CryptoManager missing_provider;
  Require(
    !missing_provider.Digest({.payload = Bytes("abc")}).HasValue(),
    "digest without provider was accepted");

  crypto::OpenSslCryptoProvider openssl;
  crypto::CryptoManager manager;
  Require(manager.RegisterProvider(openssl).HasValue(), "OpenSSL provider registration failed");
  Require(!manager.RegisterProvider(openssl).HasValue(), "duplicate provider was accepted");

  auto digest = manager.Digest({.payload = Bytes("abc")});
  Require(digest.HasValue(), "SHA-256 digest failed");
  Require(
    crypto::HexEncode(digest.Value()) ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    "SHA-256 digest vector changed");

  Require(
    !manager.ImportKey({.slot = {.id = "slot://empty"}, .material = {}}).HasValue(),
    "empty key material was accepted");
  Require(
    !manager.ImportKey({
       .slot = {.id = "slot://exportable", .exportable = true},
       .material = Bytes("secret"),
     }).HasValue(),
    "exportable key slot was accepted");

  Require(
    manager.ImportKey({
      .slot = {
        .id = "slot://hmac/test",
        .provider = crypto::ProviderKind::kOpenSsl,
        .usage = crypto::KeyUsage::kHmac,
        .exportable = false,
      },
      .material = Bytes("key"),
    }).HasValue(),
    "HMAC key import failed");

  const auto payload = Bytes("The quick brown fox jumps over the lazy dog");
  auto mac = manager.Mac({
    .key_slot_id = "slot://hmac/test",
    .payload = payload,
  });
  Require(mac.HasValue(), "HMAC calculation failed");
  Require(
    crypto::HexEncode(mac.Value()) ==
      "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8",
    "HMAC-SHA-256 test vector changed");

  auto verified = manager.VerifyMac({
    .key_slot_id = "slot://hmac/test",
    .payload = payload,
    .expected_tag = mac.Value(),
  });
  Require(verified.HasValue() && verified.Value(), "matching HMAC was rejected");

  auto tampered = mac.Value();
  tampered.back() ^= 0x01U;
  auto rejected = manager.VerifyMac({
    .key_slot_id = "slot://hmac/test",
    .payload = payload,
    .expected_tag = tampered,
  });
  Require(rejected.HasValue() && !rejected.Value(), "tampered HMAC was accepted");

  Require(
    manager.ImportKey({
      .slot = {
        .id = "slot://signature/test",
        .provider = crypto::ProviderKind::kOpenSsl,
        .usage = crypto::KeyUsage::kSignatureVerification,
        .exportable = false,
      },
      .material = Bytes("signature-key"),
    }).HasValue(),
    "signature key import failed");
  Require(
    !manager.Mac({
      .key_slot_id = "slot://signature/test",
      .payload = payload,
    }).HasValue(),
    "wrong key usage was allowed for HMAC");

  Require(
    crypto::ToString(crypto::ProviderKind::kOpenSsl) == std::string_view("OpenSSL"),
    "provider text changed");
  Require(
    crypto::ToString(crypto::Algorithm::kHmacSha256) == std::string_view("HMAC-SHA-256"),
    "algorithm text changed");
  Require(
    crypto::ToString(crypto::KeyUsage::kTlsIdentity) == std::string_view("TLSIdentity"),
    "key usage text changed");

  return 0;
}
