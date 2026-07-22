// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::safety::hardware_acceleration {

enum class AcceleratorKind {
  kCpuFallback,
  kGpu,
  kNpu,
  kDsp,
  kFpga,
  kCustom,
};

enum class IsolationLevel {
  kNone,
  kProcess,
  kCgroupDevice,
  kIommu,
  kHypervisor,
};

enum class MemoryOwnership {
  kCallerOwnedReadOnly,
  kServiceOwnedCopy,
  kDmaMappedPinned,
};

enum class SafetyMechanism {
  kIsolation,
  kMemoryOwnership,
  kJobTimeout,
  kDeterministicFailure,
  kResultPlausibility,
  kDataCorruptionDetection,
  kInterferenceControl,
  kSchedulingControl,
  kVersionedKernel,
  kFallbackPath,
  kSafetyCoverage,
};

enum class JobStatus {
  kCompleted,
  kRejected,
  kTimedOut,
  kPlausibilityFailed,
  kDataCorruptionDetected,
  kFallbackUsed,
};

struct KernelVersion final {
  std::string id;
  std::string version;
  std::string safety_hash;
};

struct ProviderDescriptor final {
  std::string id;
  AcceleratorKind kind{AcceleratorKind::kCpuFallback};
  IsolationLevel isolation{IsolationLevel::kNone};
  MemoryOwnership memory_ownership{MemoryOwnership::kCallerOwnedReadOnly};
  std::uint32_t max_parallel_jobs{1U};
  std::size_t max_input_bytes{0U};
  std::size_t max_output_bytes{0U};
  std::uint32_t max_timeout_ms{0U};
  std::vector<KernelVersion> kernels;
  std::vector<SafetyMechanism> mechanisms;
};

struct AccelerationPolicy final {
  std::string application_id;
  IsolationLevel min_isolation{IsolationLevel::kProcess};
  MemoryOwnership required_memory{MemoryOwnership::kServiceOwnedCopy};
  std::size_t max_input_bytes{4096U};
  std::size_t max_output_bytes{4096U};
  std::uint32_t max_timeout_ms{100U};
  bool allow_cpu_fallback{true};
  std::string fallback_provider_id;
  std::vector<SafetyMechanism> required_mechanisms;
};

struct AccelerationJob final {
  std::string job_id;
  std::string kernel_id;
  std::size_t input_bytes{0U};
  std::size_t expected_output_bytes{0U};
  std::uint32_t timeout_ms{0U};
  std::uint8_t priority{0U};
  bool fallback_allowed{true};
  std::uint64_t monotonic_ms{0U};
};

struct ResultObservation final {
  std::uint32_t elapsed_ms{0U};
  std::size_t output_bytes{0U};
  std::uint32_t output_checksum{0U};
  bool plausible{true};
  bool data_corruption_detected{false};
};

struct ProviderGap final {
  std::string provider_id;
  SafetyMechanism mechanism{SafetyMechanism::kIsolation};
  std::string required;
  std::string advertised;
};

struct JobReceipt final {
  JobStatus status{JobStatus::kRejected};
  std::string provider_id;
  std::string job_id;
  std::string kernel_id;
  bool used_fallback{false};
  std::uint64_t sequence{0U};
  std::uint64_t monotonic_ms{0U};
  std::uint32_t elapsed_ms{0U};
  std::size_t output_bytes{0U};
  std::uint32_t output_checksum{0U};
  std::string reason;
  std::vector<ProviderGap> gaps;
};

struct ServiceSnapshot final {
  std::uint64_t accepted_jobs{0U};
  std::uint64_t rejected_jobs{0U};
  std::uint64_t fallback_jobs{0U};
  std::uint64_t timeout_failures{0U};
  std::uint64_t plausibility_failures{0U};
  std::uint64_t corruption_failures{0U};
  std::vector<JobReceipt> receipts;
};

class SafeHardwareAccelerationService final {
public:
  [[nodiscard]] static core::Result<SafeHardwareAccelerationService> Create(
    AccelerationPolicy policy,
    std::vector<ProviderDescriptor> providers);

  [[nodiscard]] core::Result<JobReceipt> SubmitJob(
    const AccelerationJob& job,
    std::span<const std::uint8_t> input,
    const ResultObservation& observation);
  [[nodiscard]] ServiceSnapshot Snapshot() const;

private:
  SafeHardwareAccelerationService(
    AccelerationPolicy policy,
    std::vector<ProviderDescriptor> providers) noexcept;

  [[nodiscard]] core::Result<bool> ValidateJob(
    const AccelerationJob& job,
    std::span<const std::uint8_t> input) const;
  [[nodiscard]] const ProviderDescriptor* SelectProvider(
    const AccelerationJob& job,
    bool allow_fallback) const;
  [[nodiscard]] std::vector<ProviderGap> GapsForProvider(
    const ProviderDescriptor& provider) const;
  [[nodiscard]] bool SupportsKernel(
    const ProviderDescriptor& provider,
    std::string_view kernel_id) const;
  [[nodiscard]] JobReceipt MakeReceipt(
    const AccelerationJob& job,
    const ProviderDescriptor* provider,
    bool used_fallback,
    const ResultObservation& observation) const;
  void Record(JobReceipt receipt);

  AccelerationPolicy policy_{};
  std::vector<ProviderDescriptor> providers_;
  std::uint64_t sequence_{0U};
  ServiceSnapshot snapshot_{};
};

[[nodiscard]] std::string_view ToString(AcceleratorKind kind) noexcept;
[[nodiscard]] std::string_view ToString(IsolationLevel level) noexcept;
[[nodiscard]] std::string_view ToString(MemoryOwnership ownership) noexcept;
[[nodiscard]] std::string_view ToString(SafetyMechanism mechanism) noexcept;
[[nodiscard]] std::string_view ToString(JobStatus status) noexcept;

}  // namespace openautosar::safety::hardware_acceleration
