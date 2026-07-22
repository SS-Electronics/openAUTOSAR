// SPDX-License-Identifier: MIT

#include "openautosar/safety/safe_hardware_acceleration.h"

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

std::vector<openautosar::safety::hardware_acceleration::SafetyMechanism>
RequiredMechanisms() {
  namespace hwa = openautosar::safety::hardware_acceleration;
  return {
    hwa::SafetyMechanism::kIsolation,
    hwa::SafetyMechanism::kMemoryOwnership,
    hwa::SafetyMechanism::kJobTimeout,
    hwa::SafetyMechanism::kDeterministicFailure,
    hwa::SafetyMechanism::kResultPlausibility,
    hwa::SafetyMechanism::kDataCorruptionDetection,
    hwa::SafetyMechanism::kInterferenceControl,
    hwa::SafetyMechanism::kSchedulingControl,
    hwa::SafetyMechanism::kVersionedKernel,
    hwa::SafetyMechanism::kFallbackPath,
    hwa::SafetyMechanism::kSafetyCoverage,
  };
}

openautosar::safety::hardware_acceleration::AccelerationPolicy Policy() {
  namespace hwa = openautosar::safety::hardware_acceleration;
  return {
    .application_id = "ultrasonic-provider",
    .min_isolation = hwa::IsolationLevel::kIommu,
    .required_memory = hwa::MemoryOwnership::kServiceOwnedCopy,
    .max_input_bytes = 64U,
    .max_output_bytes = 32U,
    .max_timeout_ms = 50U,
    .allow_cpu_fallback = true,
    .fallback_provider_id = "cpu-safe-fallback",
    .required_mechanisms = RequiredMechanisms(),
  };
}

openautosar::safety::hardware_acceleration::ProviderDescriptor StrongGpuProvider() {
  namespace hwa = openautosar::safety::hardware_acceleration;
  return {
    .id = "gpu-safe-provider",
    .kind = hwa::AcceleratorKind::kGpu,
    .isolation = hwa::IsolationLevel::kIommu,
    .memory_ownership = hwa::MemoryOwnership::kServiceOwnedCopy,
    .max_parallel_jobs = 1U,
    .max_input_bytes = 64U,
    .max_output_bytes = 32U,
    .max_timeout_ms = 50U,
    .kernels = {{
      .id = "ultrasonic-filter",
      .version = "1.0.0",
      .safety_hash = "sha256:ultrasonic-filter-v1",
    }},
    .mechanisms = RequiredMechanisms(),
  };
}

openautosar::safety::hardware_acceleration::ProviderDescriptor CpuFallbackProvider() {
  namespace hwa = openautosar::safety::hardware_acceleration;
  return {
    .id = "cpu-safe-fallback",
    .kind = hwa::AcceleratorKind::kCpuFallback,
    .isolation = hwa::IsolationLevel::kIommu,
    .memory_ownership = hwa::MemoryOwnership::kServiceOwnedCopy,
    .max_parallel_jobs = 1U,
    .max_input_bytes = 64U,
    .max_output_bytes = 32U,
    .max_timeout_ms = 50U,
    .kernels = {{
      .id = "ultrasonic-filter",
      .version = "1.0.0",
      .safety_hash = "sha256:ultrasonic-filter-cpu-v1",
    }},
    .mechanisms = RequiredMechanisms(),
  };
}

openautosar::safety::hardware_acceleration::AccelerationJob Job(
  std::string_view id,
  std::uint64_t monotonic_ms) {
  return {
    .job_id = std::string(id),
    .kernel_id = "ultrasonic-filter",
    .input_bytes = 4U,
    .expected_output_bytes = 4U,
    .timeout_ms = 20U,
    .priority = 7U,
    .fallback_allowed = true,
    .monotonic_ms = monotonic_ms,
  };
}

openautosar::safety::hardware_acceleration::ResultObservation Observation(
  std::uint32_t elapsed_ms) {
  return {
    .elapsed_ms = elapsed_ms,
    .output_bytes = 4U,
    .output_checksum = 0xAABBCCDDU,
    .plausible = true,
    .data_corruption_detected = false,
  };
}

}  // namespace

int main() {
  namespace hwa = openautosar::safety::hardware_acceleration;

  const std::vector<std::uint8_t> input{0x01U, 0x02U, 0x03U, 0x04U};

  const auto invalid = hwa::SafeHardwareAccelerationService::Create({}, {});
  Require(!invalid.HasValue(), "invalid safe hardware acceleration config was accepted");

  auto service = hwa::SafeHardwareAccelerationService::Create(
    Policy(),
    {StrongGpuProvider(), CpuFallbackProvider()});
  Require(service.HasValue(), "safe hardware acceleration service did not open");

  auto completed = service.Value().SubmitJob(Job("job-complete", 100U), input, Observation(8U));
  Require(completed.HasValue(), "safe hardware acceleration job failed");
  Require(completed.Value().status == hwa::JobStatus::kCompleted, "job did not complete");
  Require(completed.Value().provider_id == "gpu-safe-provider", "wrong provider selected");
  Require(!completed.Value().used_fallback, "primary provider used fallback unexpectedly");
  Require(completed.Value().sequence == 1U, "first job sequence changed");

  auto timed_out = service.Value().SubmitJob(Job("job-timeout", 200U), input, Observation(21U));
  Require(timed_out.HasValue(), "timed-out job was not represented");
  Require(timed_out.Value().status == hwa::JobStatus::kTimedOut, "timeout not detected");

  auto corrupt_observation = Observation(8U);
  corrupt_observation.data_corruption_detected = true;
  auto corrupt = service.Value().SubmitJob(Job("job-corrupt", 300U), input, corrupt_observation);
  Require(corrupt.HasValue(), "corrupt job was not represented");
  Require(
    corrupt.Value().status == hwa::JobStatus::kDataCorruptionDetected,
    "corruption not detected");

  auto implausible_observation = Observation(8U);
  implausible_observation.output_bytes = 8U;
  auto implausible =
    service.Value().SubmitJob(Job("job-implausible", 400U), input, implausible_observation);
  Require(implausible.HasValue(), "implausible job was not represented");
  Require(
    implausible.Value().status == hwa::JobStatus::kPlausibilityFailed,
    "plausibility failure not detected");

  auto snapshot = service.Value().Snapshot();
  Require(snapshot.accepted_jobs == 1U, "accepted safe HWA count changed");
  Require(snapshot.timeout_failures == 1U, "timeout count changed");
  Require(snapshot.corruption_failures == 1U, "corruption count changed");
  Require(snapshot.plausibility_failures == 1U, "plausibility count changed");

  auto weak_gpu = StrongGpuProvider();
  weak_gpu.id = "gpu-weak-provider";
  weak_gpu.isolation = hwa::IsolationLevel::kProcess;
  weak_gpu.memory_ownership = hwa::MemoryOwnership::kCallerOwnedReadOnly;
  weak_gpu.mechanisms = {hwa::SafetyMechanism::kJobTimeout};
  auto fallback_service = hwa::SafeHardwareAccelerationService::Create(
    Policy(),
    {weak_gpu, CpuFallbackProvider()});
  Require(fallback_service.HasValue(), "fallback service did not open");
  auto fallback =
    fallback_service.Value().SubmitJob(Job("job-fallback", 500U), input, Observation(7U));
  Require(fallback.HasValue(), "fallback job was not represented");
  Require(fallback.Value().status == hwa::JobStatus::kFallbackUsed, "fallback was not used");
  Require(fallback.Value().used_fallback, "fallback receipt flag was not set");
  Require(
    fallback.Value().provider_id == "cpu-safe-fallback",
    "fallback provider id changed");
  Require(
    fallback_service.Value().Snapshot().fallback_jobs == 1U,
    "fallback count changed");

  auto no_fallback_policy = Policy();
  no_fallback_policy.allow_cpu_fallback = false;
  auto rejected_service =
    hwa::SafeHardwareAccelerationService::Create(no_fallback_policy, {weak_gpu});
  Require(rejected_service.HasValue(), "rejection service did not open");
  auto rejected =
    rejected_service.Value().SubmitJob(Job("job-rejected", 600U), input, Observation(6U));
  Require(rejected.HasValue(), "rejected job was not represented");
  Require(rejected.Value().status == hwa::JobStatus::kRejected, "weak provider was accepted");
  Require(!rejected.Value().gaps.empty(), "weak provider did not report safety gaps");

  auto invalid_job = Job("job-invalid", 700U);
  invalid_job.input_bytes = 2U;
  auto invalid_receipt =
    service.Value().SubmitJob(invalid_job, input, Observation(4U));
  Require(invalid_receipt.HasValue(), "invalid job was not represented");
  Require(
    invalid_receipt.Value().status == hwa::JobStatus::kRejected,
    "invalid job was accepted");

  Require(
    hwa::ToString(hwa::AcceleratorKind::kNpu) == std::string_view("Npu"),
    "accelerator kind text changed");
  Require(
    hwa::ToString(hwa::IsolationLevel::kIommu) == std::string_view("Iommu"),
    "isolation level text changed");
  Require(
    hwa::ToString(hwa::MemoryOwnership::kServiceOwnedCopy) ==
      std::string_view("ServiceOwnedCopy"),
    "memory ownership text changed");
  Require(
    hwa::ToString(hwa::SafetyMechanism::kSafetyCoverage) ==
      std::string_view("SafetyCoverage"),
    "safety mechanism text changed");
  Require(
    hwa::ToString(hwa::JobStatus::kFallbackUsed) == std::string_view("FallbackUsed"),
    "job status text changed");

  return 0;
}
