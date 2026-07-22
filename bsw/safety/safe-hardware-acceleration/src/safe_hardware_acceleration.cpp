// SPDX-License-Identifier: MIT

#include "openautosar/safety/safe_hardware_acceleration.h"

#include <algorithm>
#include <utility>

namespace openautosar::safety::hardware_acceleration {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"safe-hardware-acceleration", message};
}

[[nodiscard]] bool IsSafeIdentifier(std::string_view value) noexcept {
  if (value.empty() || value.size() > 96U) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || character == '-' || character == '_' ||
           character == '.' || character == '/';
  });
}

[[nodiscard]] std::uint8_t IsolationRank(IsolationLevel level) noexcept {
  switch (level) {
    case IsolationLevel::kNone:
      return 0U;
    case IsolationLevel::kProcess:
      return 1U;
    case IsolationLevel::kCgroupDevice:
      return 2U;
    case IsolationLevel::kIommu:
      return 3U;
    case IsolationLevel::kHypervisor:
      return 4U;
  }

  return 0U;
}

[[nodiscard]] bool ContainsMechanism(
  const std::vector<SafetyMechanism>& mechanisms,
  SafetyMechanism mechanism) {
  return std::find(mechanisms.begin(), mechanisms.end(), mechanism) != mechanisms.end();
}

[[nodiscard]] std::string MechanismText(SafetyMechanism mechanism) {
  return std::string(ToString(mechanism));
}

[[nodiscard]] std::string IsolationText(IsolationLevel level) {
  return std::string(ToString(level));
}

[[nodiscard]] std::string MemoryText(MemoryOwnership ownership) {
  return std::string(ToString(ownership));
}

[[nodiscard]] core::Result<bool> ValidatePolicy(const AccelerationPolicy& policy) {
  if (!IsSafeIdentifier(policy.application_id)) {
    return core::Result<bool>::FromError(MakeError("application id is invalid"));
  }

  if (policy.max_input_bytes == 0U || policy.max_output_bytes == 0U) {
    return core::Result<bool>::FromError(MakeError("job byte limits are zero"));
  }

  if (policy.max_timeout_ms == 0U) {
    return core::Result<bool>::FromError(MakeError("job timeout limit is zero"));
  }

  if (!policy.fallback_provider_id.empty() && !IsSafeIdentifier(policy.fallback_provider_id)) {
    return core::Result<bool>::FromError(MakeError("fallback provider id is invalid"));
  }

  return core::Result<bool>::FromValue(true);
}

[[nodiscard]] core::Result<bool> ValidateProvider(const ProviderDescriptor& provider) {
  if (!IsSafeIdentifier(provider.id)) {
    return core::Result<bool>::FromError(MakeError("provider id is invalid"));
  }

  if (provider.max_parallel_jobs == 0U) {
    return core::Result<bool>::FromError(MakeError("provider parallel job limit is zero"));
  }

  if (provider.max_input_bytes == 0U || provider.max_output_bytes == 0U) {
    return core::Result<bool>::FromError(MakeError("provider byte limit is zero"));
  }

  if (provider.max_timeout_ms == 0U) {
    return core::Result<bool>::FromError(MakeError("provider timeout is zero"));
  }

  if (provider.kernels.empty()) {
    return core::Result<bool>::FromError(MakeError("provider has no versioned kernels"));
  }

  for (const auto& kernel : provider.kernels) {
    if (!IsSafeIdentifier(kernel.id) || kernel.version.empty() || kernel.safety_hash.empty()) {
      return core::Result<bool>::FromError(MakeError("provider kernel metadata is invalid"));
    }
  }

  return core::Result<bool>::FromValue(true);
}

void AddGap(
  std::vector<ProviderGap>& gaps,
  std::string provider_id,
  SafetyMechanism mechanism,
  std::string required,
  std::string advertised) {
  gaps.push_back({
    .provider_id = std::move(provider_id),
    .mechanism = mechanism,
    .required = std::move(required),
    .advertised = std::move(advertised),
  });
}

}  // namespace

core::Result<SafeHardwareAccelerationService> SafeHardwareAccelerationService::Create(
  AccelerationPolicy policy,
  std::vector<ProviderDescriptor> providers) {
  auto policy_validation = ValidatePolicy(policy);
  if (!policy_validation) {
    return core::Result<SafeHardwareAccelerationService>::FromError(
      policy_validation.Error());
  }

  if (providers.empty()) {
    return core::Result<SafeHardwareAccelerationService>::FromError(
      MakeError("provider list is empty"));
  }

  for (const auto& provider : providers) {
    auto provider_validation = ValidateProvider(provider);
    if (!provider_validation) {
      return core::Result<SafeHardwareAccelerationService>::FromError(
        provider_validation.Error());
    }
  }

  return core::Result<SafeHardwareAccelerationService>::FromValue(
    SafeHardwareAccelerationService(std::move(policy), std::move(providers)));
}

core::Result<JobReceipt> SafeHardwareAccelerationService::SubmitJob(
  const AccelerationJob& job,
  std::span<const std::uint8_t> input,
  const ResultObservation& observation) {
  auto job_validation = ValidateJob(job, input);
  if (!job_validation) {
    auto receipt = MakeReceipt(job, nullptr, false, observation);
    receipt.status = JobStatus::kRejected;
    receipt.reason = job_validation.Error().message;
    Record(std::move(receipt));
    return core::Result<JobReceipt>::FromValue(snapshot_.receipts.back());
  }

  const auto* provider = SelectProvider(job, false);
  bool used_fallback{false};
  if (provider == nullptr && policy_.allow_cpu_fallback && job.fallback_allowed) {
    provider = SelectProvider(job, true);
    used_fallback = provider != nullptr;
  }

  auto receipt = MakeReceipt(job, provider, used_fallback, observation);
  if (provider == nullptr) {
    receipt.status = JobStatus::kRejected;
    receipt.reason = "no provider satisfied safety mechanisms";
    for (const auto& candidate : providers_) {
      if (SupportsKernel(candidate, job.kernel_id)) {
        auto gaps = GapsForProvider(candidate);
        receipt.gaps.insert(receipt.gaps.end(), gaps.begin(), gaps.end());
      }
    }
  } else if (observation.elapsed_ms > job.timeout_ms ||
             observation.elapsed_ms > provider->max_timeout_ms) {
    receipt.status = JobStatus::kTimedOut;
    receipt.reason = "job exceeded deterministic timeout";
  } else if (observation.data_corruption_detected) {
    receipt.status = JobStatus::kDataCorruptionDetected;
    receipt.reason = "provider reported data corruption";
  } else if (!observation.plausible ||
             observation.output_bytes > job.expected_output_bytes ||
             observation.output_bytes > provider->max_output_bytes) {
    receipt.status = JobStatus::kPlausibilityFailed;
    receipt.reason = "result failed plausibility envelope";
  } else if (used_fallback) {
    receipt.status = JobStatus::kFallbackUsed;
    receipt.reason = "cpu fallback path used";
  } else {
    receipt.status = JobStatus::kCompleted;
    receipt.reason = "job completed";
  }

  Record(std::move(receipt));
  return core::Result<JobReceipt>::FromValue(snapshot_.receipts.back());
}

ServiceSnapshot SafeHardwareAccelerationService::Snapshot() const {
  return snapshot_;
}

SafeHardwareAccelerationService::SafeHardwareAccelerationService(
  AccelerationPolicy policy,
  std::vector<ProviderDescriptor> providers) noexcept
  : policy_(std::move(policy)), providers_(std::move(providers)) {}

core::Result<bool> SafeHardwareAccelerationService::ValidateJob(
  const AccelerationJob& job,
  std::span<const std::uint8_t> input) const {
  if (!IsSafeIdentifier(job.job_id) || !IsSafeIdentifier(job.kernel_id)) {
    return core::Result<bool>::FromError(MakeError("job identifier is invalid"));
  }

  if (job.input_bytes == 0U || input.empty() || input.size() != job.input_bytes) {
    return core::Result<bool>::FromError(MakeError("job input size is invalid"));
  }

  if (job.input_bytes > policy_.max_input_bytes) {
    return core::Result<bool>::FromError(MakeError("job input exceeds policy limit"));
  }

  if (job.expected_output_bytes == 0U ||
      job.expected_output_bytes > policy_.max_output_bytes) {
    return core::Result<bool>::FromError(MakeError("job output limit is invalid"));
  }

  if (job.timeout_ms == 0U || job.timeout_ms > policy_.max_timeout_ms) {
    return core::Result<bool>::FromError(MakeError("job timeout exceeds policy limit"));
  }

  return core::Result<bool>::FromValue(true);
}

const ProviderDescriptor* SafeHardwareAccelerationService::SelectProvider(
  const AccelerationJob& job,
  bool allow_fallback) const {
  const ProviderDescriptor* selected{nullptr};
  for (const auto& provider : providers_) {
    if (!SupportsKernel(provider, job.kernel_id) || !GapsForProvider(provider).empty()) {
      continue;
    }

    const auto is_fallback = provider.kind == AcceleratorKind::kCpuFallback;
    if (allow_fallback != is_fallback) {
      continue;
    }

    if (allow_fallback && !policy_.fallback_provider_id.empty() &&
        provider.id != policy_.fallback_provider_id) {
      continue;
    }

    if (job.input_bytes > provider.max_input_bytes ||
        job.expected_output_bytes > provider.max_output_bytes ||
        job.timeout_ms > provider.max_timeout_ms) {
      continue;
    }

    selected = &provider;
    break;
  }

  return selected;
}

std::vector<ProviderGap> SafeHardwareAccelerationService::GapsForProvider(
  const ProviderDescriptor& provider) const {
  std::vector<ProviderGap> gaps;
  if (IsolationRank(provider.isolation) < IsolationRank(policy_.min_isolation)) {
    AddGap(
      gaps,
      provider.id,
      SafetyMechanism::kIsolation,
      IsolationText(policy_.min_isolation),
      IsolationText(provider.isolation));
  }

  if (provider.memory_ownership != policy_.required_memory) {
    AddGap(
      gaps,
      provider.id,
      SafetyMechanism::kMemoryOwnership,
      MemoryText(policy_.required_memory),
      MemoryText(provider.memory_ownership));
  }

  for (const auto mechanism : policy_.required_mechanisms) {
    if (!ContainsMechanism(provider.mechanisms, mechanism)) {
      AddGap(gaps, provider.id, mechanism, MechanismText(mechanism), "missing");
    }
  }

  return gaps;
}

bool SafeHardwareAccelerationService::SupportsKernel(
  const ProviderDescriptor& provider,
  std::string_view kernel_id) const {
  return std::any_of(provider.kernels.begin(), provider.kernels.end(),
                     [kernel_id](const auto& kernel) { return kernel.id == kernel_id; });
}

JobReceipt SafeHardwareAccelerationService::MakeReceipt(
  const AccelerationJob& job,
  const ProviderDescriptor* provider,
  bool used_fallback,
  const ResultObservation& observation) const {
  return {
    .status = JobStatus::kRejected,
    .provider_id = provider == nullptr ? std::string{} : provider->id,
    .job_id = job.job_id,
    .kernel_id = job.kernel_id,
    .used_fallback = used_fallback,
    .sequence = sequence_ + 1U,
    .monotonic_ms = job.monotonic_ms,
    .elapsed_ms = observation.elapsed_ms,
    .output_bytes = observation.output_bytes,
    .output_checksum = observation.output_checksum,
    .reason = {},
    .gaps = {},
  };
}

void SafeHardwareAccelerationService::Record(JobReceipt receipt) {
  sequence_ = receipt.sequence;
  switch (receipt.status) {
    case JobStatus::kCompleted:
      ++snapshot_.accepted_jobs;
      break;
    case JobStatus::kFallbackUsed:
      ++snapshot_.accepted_jobs;
      ++snapshot_.fallback_jobs;
      break;
    case JobStatus::kTimedOut:
      ++snapshot_.rejected_jobs;
      ++snapshot_.timeout_failures;
      break;
    case JobStatus::kPlausibilityFailed:
      ++snapshot_.rejected_jobs;
      ++snapshot_.plausibility_failures;
      break;
    case JobStatus::kDataCorruptionDetected:
      ++snapshot_.rejected_jobs;
      ++snapshot_.corruption_failures;
      break;
    case JobStatus::kRejected:
      ++snapshot_.rejected_jobs;
      break;
  }

  snapshot_.receipts.push_back(std::move(receipt));
}

std::string_view ToString(AcceleratorKind kind) noexcept {
  switch (kind) {
    case AcceleratorKind::kCpuFallback:
      return "CpuFallback";
    case AcceleratorKind::kGpu:
      return "Gpu";
    case AcceleratorKind::kNpu:
      return "Npu";
    case AcceleratorKind::kDsp:
      return "Dsp";
    case AcceleratorKind::kFpga:
      return "Fpga";
    case AcceleratorKind::kCustom:
      return "Custom";
  }

  return "Unknown";
}

std::string_view ToString(IsolationLevel level) noexcept {
  switch (level) {
    case IsolationLevel::kNone:
      return "None";
    case IsolationLevel::kProcess:
      return "Process";
    case IsolationLevel::kCgroupDevice:
      return "CgroupDevice";
    case IsolationLevel::kIommu:
      return "Iommu";
    case IsolationLevel::kHypervisor:
      return "Hypervisor";
  }

  return "Unknown";
}

std::string_view ToString(MemoryOwnership ownership) noexcept {
  switch (ownership) {
    case MemoryOwnership::kCallerOwnedReadOnly:
      return "CallerOwnedReadOnly";
    case MemoryOwnership::kServiceOwnedCopy:
      return "ServiceOwnedCopy";
    case MemoryOwnership::kDmaMappedPinned:
      return "DmaMappedPinned";
  }

  return "Unknown";
}

std::string_view ToString(SafetyMechanism mechanism) noexcept {
  switch (mechanism) {
    case SafetyMechanism::kIsolation:
      return "Isolation";
    case SafetyMechanism::kMemoryOwnership:
      return "MemoryOwnership";
    case SafetyMechanism::kJobTimeout:
      return "JobTimeout";
    case SafetyMechanism::kDeterministicFailure:
      return "DeterministicFailure";
    case SafetyMechanism::kResultPlausibility:
      return "ResultPlausibility";
    case SafetyMechanism::kDataCorruptionDetection:
      return "DataCorruptionDetection";
    case SafetyMechanism::kInterferenceControl:
      return "InterferenceControl";
    case SafetyMechanism::kSchedulingControl:
      return "SchedulingControl";
    case SafetyMechanism::kVersionedKernel:
      return "VersionedKernel";
    case SafetyMechanism::kFallbackPath:
      return "FallbackPath";
    case SafetyMechanism::kSafetyCoverage:
      return "SafetyCoverage";
  }

  return "Unknown";
}

std::string_view ToString(JobStatus status) noexcept {
  switch (status) {
    case JobStatus::kCompleted:
      return "Completed";
    case JobStatus::kRejected:
      return "Rejected";
    case JobStatus::kTimedOut:
      return "TimedOut";
    case JobStatus::kPlausibilityFailed:
      return "PlausibilityFailed";
    case JobStatus::kDataCorruptionDetected:
      return "DataCorruptionDetected";
    case JobStatus::kFallbackUsed:
      return "FallbackUsed";
  }

  return "Unknown";
}

}  // namespace openautosar::safety::hardware_acceleration
