// SPDX-License-Identifier: MIT

#pragma once

#include "openautosar/core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::raw_data_stream {

enum class BufferPolicy {
  kRejectNewest,
  kDropOldest,
};

enum class ReceiptStatus {
  kAccepted,
  kReassembled,
  kBackpressureRejected,
  kDroppedOldest,
  kStale,
  kDuplicate,
  kMalformed,
  kCorrupt,
  kUnsupported,
};

enum class StreamState {
  kReady,
  kDegraded,
};

struct DeploymentTrace final {
  std::string service_instance;
  std::string source_model_path;
  std::string source_model_pointer;
  std::string deployment_ref;
};

struct StreamConfig final {
  std::string channel_id;
  std::size_t max_frame_payload_bytes{256U};
  std::size_t max_sample_bytes{1024U};
  std::uint16_t max_fragments_per_sample{8U};
  std::size_t queue_depth{4U};
  std::uint32_t stale_timeout_ms{100U};
  BufferPolicy buffer_policy{BufferPolicy::kRejectNewest};
  bool require_monotonic_time{true};
  bool allow_fragment_reassembly{true};
  DeploymentTrace trace{};
};

struct FrameDescriptor final {
  std::string channel_id;
  std::uint64_t sequence{0U};
  std::uint16_t fragment_index{0U};
  std::uint16_t fragment_count{1U};
  std::uint64_t monotonic_ms{0U};
  std::uint32_t payload_checksum{0U};
  std::vector<std::uint8_t> payload;
};

struct StreamSample final {
  std::string channel_id;
  std::uint64_t sequence{0U};
  std::uint64_t monotonic_ms{0U};
  std::uint16_t fragment_count{1U};
  std::uint32_t payload_checksum{0U};
  std::vector<std::uint8_t> payload;
  DeploymentTrace trace{};
};

struct StreamReceipt final {
  ReceiptStatus status{ReceiptStatus::kAccepted};
  std::uint64_t sequence{0U};
  std::uint64_t dropped_sequence{0U};
  std::size_t queued_samples{0U};
  std::string reason;
};

struct StreamSnapshot final {
  StreamState state{StreamState::kReady};
  std::uint64_t accepted_frames{0U};
  std::uint64_t rejected_frames{0U};
  std::uint64_t dropped_samples{0U};
  std::uint64_t stale_frames{0U};
  std::uint64_t duplicate_frames{0U};
  std::uint64_t corrupt_frames{0U};
  std::uint64_t reassembled_samples{0U};
  std::size_t queued_samples{0U};
  std::size_t pending_sequences{0U};
  DeploymentTrace trace{};
};

class RawDataStreamChannel final {
public:
  [[nodiscard]] static core::Result<RawDataStreamChannel> Open(StreamConfig config);

  [[nodiscard]] core::Result<StreamReceipt> Push(FrameDescriptor frame);
  [[nodiscard]] core::Result<StreamSample> Poll();
  [[nodiscard]] StreamSnapshot Snapshot() const;

private:
  struct PendingSample final {
    std::uint64_t sequence{0U};
    std::uint64_t monotonic_ms{0U};
    std::vector<std::vector<std::uint8_t>> fragments;
    std::vector<bool> received;
    std::size_t total_bytes{0U};
  };

  explicit RawDataStreamChannel(StreamConfig config) noexcept;

  [[nodiscard]] core::Result<bool> ValidateFrame(const FrameDescriptor& frame) const;
  [[nodiscard]] bool IsDuplicateFrame(const FrameDescriptor& frame) const;
  [[nodiscard]] bool IsStaleFrame(const FrameDescriptor& frame) const noexcept;
  [[nodiscard]] StreamReceipt Reject(
    ReceiptStatus status,
    std::uint64_t sequence,
    std::string reason);
  [[nodiscard]] StreamReceipt Enqueue(StreamSample sample, ReceiptStatus status);
  [[nodiscard]] StreamReceipt PushUnfragmented(FrameDescriptor frame);
  [[nodiscard]] StreamReceipt PushFragmented(FrameDescriptor frame);
  [[nodiscard]] bool PendingComplete(const PendingSample& pending) const;
  [[nodiscard]] StreamSample Assemble(const PendingSample& pending) const;
  [[nodiscard]] bool WasCompleted(std::uint64_t sequence) const;
  void RememberCompleted(std::uint64_t sequence);

  StreamConfig config_{};
  std::vector<StreamSample> queue_;
  std::vector<PendingSample> pending_;
  std::vector<std::uint64_t> completed_sequences_;
  std::uint64_t latest_monotonic_ms_{0U};
  StreamSnapshot snapshot_{};
};

[[nodiscard]] std::uint32_t CalculateChecksum(std::span<const std::uint8_t> payload) noexcept;
[[nodiscard]] std::string_view ToString(BufferPolicy policy) noexcept;
[[nodiscard]] std::string_view ToString(ReceiptStatus status) noexcept;
[[nodiscard]] std::string_view ToString(StreamState state) noexcept;

}  // namespace openautosar::raw_data_stream
