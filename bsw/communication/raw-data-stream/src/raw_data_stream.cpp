// SPDX-License-Identifier: MIT

#include "openautosar/raw_data_stream/raw_data_stream.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace openautosar::raw_data_stream {
namespace {

[[nodiscard]] core::ErrorCode MakeError(const char* message) {
  return {"raw-data-stream", message};
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

[[nodiscard]] core::Result<bool> ValidateConfig(const StreamConfig& config) {
  if (!IsSafeIdentifier(config.channel_id)) {
    return core::Result<bool>::FromError(MakeError("stream channel id is invalid"));
  }

  if (config.max_frame_payload_bytes == 0U || config.max_sample_bytes == 0U) {
    return core::Result<bool>::FromError(MakeError("stream payload limits are zero"));
  }

  if (config.max_frame_payload_bytes > config.max_sample_bytes) {
    return core::Result<bool>::FromError(MakeError("frame limit exceeds sample limit"));
  }

  if (config.max_fragments_per_sample == 0U || config.queue_depth == 0U) {
    return core::Result<bool>::FromError(MakeError("stream queue or fragment limit is zero"));
  }

  if (config.stale_timeout_ms == 0U) {
    return core::Result<bool>::FromError(MakeError("stream stale timeout is zero"));
  }

  return core::Result<bool>::FromValue(true);
}

}  // namespace

std::uint32_t CalculateChecksum(std::span<const std::uint8_t> payload) noexcept {
  std::uint32_t checksum{2166136261U};
  for (const auto byte : payload) {
    checksum ^= byte;
    checksum *= 16777619U;
  }
  return checksum;
}

core::Result<RawDataStreamChannel> RawDataStreamChannel::Open(StreamConfig config) {
  auto validation = ValidateConfig(config);
  if (!validation) {
    return core::Result<RawDataStreamChannel>::FromError(validation.Error());
  }

  return core::Result<RawDataStreamChannel>::FromValue(
    RawDataStreamChannel(std::move(config)));
}

core::Result<StreamReceipt> RawDataStreamChannel::Push(FrameDescriptor frame) {
  auto validation = ValidateFrame(frame);
  if (!validation) {
    return core::Result<StreamReceipt>::FromValue(
      Reject(ReceiptStatus::kMalformed, frame.sequence, validation.Error().message));
  }

  if (frame.payload_checksum != 0U &&
      frame.payload_checksum != CalculateChecksum(frame.payload)) {
    return core::Result<StreamReceipt>::FromValue(
      Reject(ReceiptStatus::kCorrupt, frame.sequence, "frame checksum mismatch"));
  }

  if (IsStaleFrame(frame)) {
    return core::Result<StreamReceipt>::FromValue(
      Reject(ReceiptStatus::kStale, frame.sequence, "frame is stale"));
  }
  latest_monotonic_ms_ = std::max(latest_monotonic_ms_, frame.monotonic_ms);

  if (IsDuplicateFrame(frame)) {
    return core::Result<StreamReceipt>::FromValue(
      Reject(ReceiptStatus::kDuplicate, frame.sequence, "duplicate stream frame"));
  }

  ++snapshot_.accepted_frames;
  if (frame.fragment_count == 1U) {
    return core::Result<StreamReceipt>::FromValue(PushUnfragmented(std::move(frame)));
  }

  return core::Result<StreamReceipt>::FromValue(PushFragmented(std::move(frame)));
}

core::Result<StreamSample> RawDataStreamChannel::Poll() {
  if (queue_.empty()) {
    return core::Result<StreamSample>::FromError(MakeError("raw data stream queue is empty"));
  }

  auto sample = std::move(queue_.front());
  queue_.erase(queue_.begin());
  snapshot_.queued_samples = queue_.size();
  return core::Result<StreamSample>::FromValue(std::move(sample));
}

StreamSnapshot RawDataStreamChannel::Snapshot() const {
  auto snapshot = snapshot_;
  snapshot.queued_samples = queue_.size();
  snapshot.pending_sequences = pending_.size();
  snapshot.trace = config_.trace;
  if (snapshot.rejected_frames > 0U || snapshot.dropped_samples > 0U) {
    snapshot.state = StreamState::kDegraded;
  }
  return snapshot;
}

RawDataStreamChannel::RawDataStreamChannel(StreamConfig config) noexcept
  : config_(std::move(config)) {
  snapshot_.trace = config_.trace;
}

core::Result<bool> RawDataStreamChannel::ValidateFrame(const FrameDescriptor& frame) const {
  if (frame.channel_id != config_.channel_id) {
    return core::Result<bool>::FromError(MakeError("frame channel does not match stream"));
  }

  if (frame.sequence == 0U) {
    return core::Result<bool>::FromError(MakeError("stream sequence is zero"));
  }

  if (frame.payload.empty() || frame.payload.size() > config_.max_frame_payload_bytes) {
    return core::Result<bool>::FromError(MakeError("frame payload size is invalid"));
  }

  if (frame.fragment_count == 0U || frame.fragment_index >= frame.fragment_count) {
    return core::Result<bool>::FromError(MakeError("frame fragment index is invalid"));
  }

  if (frame.fragment_count > config_.max_fragments_per_sample) {
    return core::Result<bool>::FromError(MakeError("frame fragment count exceeds policy"));
  }

  if (frame.fragment_count > 1U && !config_.allow_fragment_reassembly) {
    return core::Result<bool>::FromError(MakeError("stream reassembly is disabled"));
  }

  return core::Result<bool>::FromValue(true);
}

bool RawDataStreamChannel::IsDuplicateFrame(const FrameDescriptor& frame) const {
  if (WasCompleted(frame.sequence)) {
    return true;
  }

  const auto queued = std::any_of(queue_.begin(), queue_.end(), [&frame](const auto& sample) {
    return sample.sequence == frame.sequence;
  });
  if (queued) {
    return true;
  }

  const auto pending_iter = std::find_if(
    pending_.begin(),
    pending_.end(),
    [&frame](const auto& pending) { return pending.sequence == frame.sequence; });
  if (pending_iter == pending_.end()) {
    return false;
  }

  if (frame.fragment_index >= pending_iter->received.size()) {
    return true;
  }

  return pending_iter->received[frame.fragment_index];
}

bool RawDataStreamChannel::IsStaleFrame(const FrameDescriptor& frame) const noexcept {
  if (!config_.require_monotonic_time || latest_monotonic_ms_ == 0U) {
    return false;
  }

  return frame.monotonic_ms + config_.stale_timeout_ms < latest_monotonic_ms_;
}

StreamReceipt RawDataStreamChannel::Reject(
  ReceiptStatus status,
  std::uint64_t sequence,
  std::string reason) {
  ++snapshot_.rejected_frames;
  if (status == ReceiptStatus::kStale) {
    ++snapshot_.stale_frames;
  } else if (status == ReceiptStatus::kDuplicate) {
    ++snapshot_.duplicate_frames;
  } else if (status == ReceiptStatus::kCorrupt) {
    ++snapshot_.corrupt_frames;
  }

  return {
    .status = status,
    .sequence = sequence,
    .dropped_sequence = 0U,
    .queued_samples = queue_.size(),
    .reason = std::move(reason),
  };
}

StreamReceipt RawDataStreamChannel::Enqueue(StreamSample sample, ReceiptStatus status) {
  StreamReceipt receipt{
    .status = status,
    .sequence = sample.sequence,
    .dropped_sequence = 0U,
    .queued_samples = queue_.size(),
    .reason = {},
  };

  if (queue_.size() >= config_.queue_depth) {
    if (config_.buffer_policy == BufferPolicy::kRejectNewest) {
      ++snapshot_.rejected_frames;
      receipt.status = ReceiptStatus::kBackpressureRejected;
      receipt.reason = "raw data stream queue is full";
      receipt.queued_samples = queue_.size();
      return receipt;
    }

    receipt.status = ReceiptStatus::kDroppedOldest;
    receipt.dropped_sequence = queue_.front().sequence;
    queue_.erase(queue_.begin());
    ++snapshot_.dropped_samples;
  }

  RememberCompleted(sample.sequence);
  queue_.push_back(std::move(sample));
  snapshot_.queued_samples = queue_.size();
  receipt.queued_samples = queue_.size();
  return receipt;
}

StreamReceipt RawDataStreamChannel::PushUnfragmented(FrameDescriptor frame) {
  StreamSample sample{
    .channel_id = frame.channel_id,
    .sequence = frame.sequence,
    .monotonic_ms = frame.monotonic_ms,
    .fragment_count = 1U,
    .payload_checksum = CalculateChecksum(frame.payload),
    .payload = std::move(frame.payload),
    .trace = config_.trace,
  };
  return Enqueue(std::move(sample), ReceiptStatus::kAccepted);
}

StreamReceipt RawDataStreamChannel::PushFragmented(FrameDescriptor frame) {
  auto pending_iter = std::find_if(
    pending_.begin(),
    pending_.end(),
    [&frame](const auto& pending) { return pending.sequence == frame.sequence; });
  if (pending_iter == pending_.end()) {
    PendingSample pending{
      .sequence = frame.sequence,
      .monotonic_ms = frame.monotonic_ms,
      .fragments = std::vector<std::vector<std::uint8_t>>(frame.fragment_count),
      .received = std::vector<bool>(frame.fragment_count, false),
      .total_bytes = 0U,
    };
    pending_.push_back(std::move(pending));
    pending_iter = std::prev(pending_.end());
  }

  pending_iter->fragments[frame.fragment_index] = std::move(frame.payload);
  pending_iter->received[frame.fragment_index] = true;
  pending_iter->total_bytes += pending_iter->fragments[frame.fragment_index].size();
  pending_iter->monotonic_ms = std::max(pending_iter->monotonic_ms, frame.monotonic_ms);
  if (pending_iter->total_bytes > config_.max_sample_bytes) {
    const auto sequence = pending_iter->sequence;
    pending_.erase(pending_iter);
    return Reject(ReceiptStatus::kMalformed, sequence, "reassembled sample exceeds policy");
  }

  if (!PendingComplete(*pending_iter)) {
    snapshot_.pending_sequences = pending_.size();
    return {
      .status = ReceiptStatus::kAccepted,
      .sequence = frame.sequence,
      .dropped_sequence = 0U,
      .queued_samples = queue_.size(),
      .reason = "fragment accepted",
    };
  }

  auto sample = Assemble(*pending_iter);
  pending_.erase(pending_iter);
  ++snapshot_.reassembled_samples;
  snapshot_.pending_sequences = pending_.size();
  return Enqueue(std::move(sample), ReceiptStatus::kReassembled);
}

bool RawDataStreamChannel::PendingComplete(const PendingSample& pending) const {
  return std::all_of(pending.received.begin(), pending.received.end(), [](bool value) {
    return value;
  });
}

StreamSample RawDataStreamChannel::Assemble(const PendingSample& pending) const {
  std::vector<std::uint8_t> payload;
  payload.reserve(pending.total_bytes);
  for (const auto& fragment : pending.fragments) {
    payload.insert(payload.end(), fragment.begin(), fragment.end());
  }

  return {
    .channel_id = config_.channel_id,
    .sequence = pending.sequence,
    .monotonic_ms = pending.monotonic_ms,
    .fragment_count = static_cast<std::uint16_t>(pending.fragments.size()),
    .payload_checksum = CalculateChecksum(payload),
    .payload = std::move(payload),
    .trace = config_.trace,
  };
}

bool RawDataStreamChannel::WasCompleted(std::uint64_t sequence) const {
  return std::find(completed_sequences_.begin(), completed_sequences_.end(), sequence) !=
         completed_sequences_.end();
}

void RawDataStreamChannel::RememberCompleted(std::uint64_t sequence) {
  completed_sequences_.push_back(sequence);
  if (completed_sequences_.size() > config_.queue_depth + config_.max_fragments_per_sample) {
    completed_sequences_.erase(completed_sequences_.begin());
  }
}

std::string_view ToString(BufferPolicy policy) noexcept {
  switch (policy) {
    case BufferPolicy::kRejectNewest:
      return "RejectNewest";
    case BufferPolicy::kDropOldest:
      return "DropOldest";
  }
  return "Unknown";
}

std::string_view ToString(ReceiptStatus status) noexcept {
  switch (status) {
    case ReceiptStatus::kAccepted:
      return "Accepted";
    case ReceiptStatus::kReassembled:
      return "Reassembled";
    case ReceiptStatus::kBackpressureRejected:
      return "BackpressureRejected";
    case ReceiptStatus::kDroppedOldest:
      return "DroppedOldest";
    case ReceiptStatus::kStale:
      return "Stale";
    case ReceiptStatus::kDuplicate:
      return "Duplicate";
    case ReceiptStatus::kMalformed:
      return "Malformed";
    case ReceiptStatus::kCorrupt:
      return "Corrupt";
    case ReceiptStatus::kUnsupported:
      return "Unsupported";
  }
  return "Unknown";
}

std::string_view ToString(StreamState state) noexcept {
  switch (state) {
    case StreamState::kReady:
      return "Ready";
    case StreamState::kDegraded:
      return "Degraded";
  }
  return "Unknown";
}

}  // namespace openautosar::raw_data_stream
