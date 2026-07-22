// SPDX-License-Identifier: MIT

#include "openautosar/raw_data_stream/raw_data_stream.h"

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

openautosar::raw_data_stream::StreamConfig Config(
  openautosar::raw_data_stream::BufferPolicy policy =
    openautosar::raw_data_stream::BufferPolicy::kRejectNewest) {
  return {
    .channel_id = "camera/front/raw",
    .max_frame_payload_bytes = 4U,
    .max_sample_bytes = 12U,
    .max_fragments_per_sample = 4U,
    .queue_depth = 2U,
    .stale_timeout_ms = 10U,
    .buffer_policy = policy,
    .require_monotonic_time = true,
    .allow_fragment_reassembly = true,
    .trace = {
      .service_instance = "/OpenAUTOSAR/Vehicle/Camera/Front",
      .source_model_path = "camera_raw_stream.json",
      .source_model_pointer = "/streams/0",
      .deployment_ref = "raw-data-stream/front-camera",
    },
  };
}

openautosar::raw_data_stream::FrameDescriptor Frame(
  std::uint64_t sequence,
  std::vector<std::uint8_t> payload,
  std::uint64_t monotonic_ms) {
  return {
    .channel_id = "camera/front/raw",
    .sequence = sequence,
    .fragment_index = 0U,
    .fragment_count = 1U,
    .monotonic_ms = monotonic_ms,
    .payload_checksum = 0U,
    .payload = std::move(payload),
  };
}

openautosar::raw_data_stream::FrameDescriptor Fragment(
  std::uint64_t sequence,
  std::uint16_t index,
  std::uint16_t count,
  std::vector<std::uint8_t> payload,
  std::uint64_t monotonic_ms) {
  return {
    .channel_id = "camera/front/raw",
    .sequence = sequence,
    .fragment_index = index,
    .fragment_count = count,
    .monotonic_ms = monotonic_ms,
    .payload_checksum = 0U,
    .payload = std::move(payload),
  };
}

}  // namespace

int main() {
  namespace rds = openautosar::raw_data_stream;

  const auto invalid = rds::RawDataStreamChannel::Open({});
  Require(!invalid.HasValue(), "invalid raw data stream config was accepted");

  auto stream = rds::RawDataStreamChannel::Open(Config());
  Require(stream.HasValue(), "raw data stream did not open");

  auto accepted = stream.Value().Push(Frame(1U, {0x10U, 0x20U}, 100U));
  Require(accepted.HasValue(), "raw data stream push failed");
  Require(
    accepted.Value().status == rds::ReceiptStatus::kAccepted,
    "complete frame was not accepted");
  Require(accepted.Value().queued_samples == 1U, "queued sample count changed");

  auto sample = stream.Value().Poll();
  Require(sample.HasValue(), "raw data stream poll failed");
  Require(sample.Value().sequence == 1U, "polled sample sequence changed");
  Require(
    sample.Value().payload == std::vector<std::uint8_t>({0x10U, 0x20U}),
    "polled payload changed");
  Require(
    sample.Value().trace.deployment_ref == "raw-data-stream/front-camera",
    "raw data stream trace changed");

  Require(!stream.Value().Poll().HasValue(), "empty stream poll succeeded");

  auto partial = stream.Value().Push(Fragment(2U, 0U, 3U, {0x01U, 0x02U}, 110U));
  Require(partial.HasValue(), "first fragment failed");
  Require(partial.Value().status == rds::ReceiptStatus::kAccepted, "first fragment rejected");
  Require(stream.Value().Snapshot().pending_sequences == 1U, "pending sequence count changed");

  Require(
    stream.Value().Push(Fragment(2U, 2U, 3U, {0x05U}, 112U)).Value().status ==
      rds::ReceiptStatus::kAccepted,
    "out-of-order fragment was rejected");
  auto reassembled = stream.Value().Push(Fragment(2U, 1U, 3U, {0x03U, 0x04U}, 111U));
  Require(reassembled.HasValue(), "final fragment failed");
  Require(
    reassembled.Value().status == rds::ReceiptStatus::kReassembled,
    "fragmented sample was not reassembled");
  Require(stream.Value().Snapshot().reassembled_samples == 1U, "reassembled count changed");
  auto assembled = stream.Value().Poll();
  Require(assembled.HasValue(), "reassembled sample missing");
  Require(
    assembled.Value().payload ==
      std::vector<std::uint8_t>({0x01U, 0x02U, 0x03U, 0x04U, 0x05U}),
    "reassembled payload order changed");

  auto duplicate = stream.Value().Push(Frame(2U, {0xAAU}, 113U));
  Require(duplicate.HasValue(), "duplicate frame was not represented");
  Require(
    duplicate.Value().status == rds::ReceiptStatus::kDuplicate,
    "duplicate frame was accepted");

  auto stale = stream.Value().Push(Frame(3U, {0xAAU}, 90U));
  Require(stale.HasValue(), "stale frame was not represented");
  Require(stale.Value().status == rds::ReceiptStatus::kStale, "stale frame was accepted");

  auto corrupt_frame = Frame(4U, {0xAAU}, 120U);
  corrupt_frame.payload_checksum = 0x1234U;
  auto corrupt = stream.Value().Push(corrupt_frame);
  Require(corrupt.HasValue(), "corrupt frame was not represented");
  Require(
    corrupt.Value().status == rds::ReceiptStatus::kCorrupt,
    "corrupt frame was accepted");

  auto unsupported_config = Config();
  unsupported_config.allow_fragment_reassembly = false;
  auto unsupported_stream = rds::RawDataStreamChannel::Open(unsupported_config);
  Require(unsupported_stream.HasValue(), "unsupported stream config did not open");
  auto unsupported =
    unsupported_stream.Value().Push(Fragment(5U, 0U, 2U, {0x01U}, 130U));
  Require(unsupported.HasValue(), "unsupported fragment was not represented");
  Require(
    unsupported.Value().status == rds::ReceiptStatus::kMalformed,
    "disabled reassembly was accepted");

  auto drop_stream =
    rds::RawDataStreamChannel::Open(Config(rds::BufferPolicy::kDropOldest));
  Require(drop_stream.HasValue(), "drop-oldest stream did not open");
  Require(drop_stream.Value().Push(Frame(10U, {0x01U}, 200U)).HasValue(), "push 10 failed");
  Require(drop_stream.Value().Push(Frame(11U, {0x02U}, 201U)).HasValue(), "push 11 failed");
  auto dropped = drop_stream.Value().Push(Frame(12U, {0x03U}, 202U));
  Require(dropped.HasValue(), "drop-oldest push failed");
  Require(
    dropped.Value().status == rds::ReceiptStatus::kDroppedOldest,
    "drop-oldest policy did not drop oldest sample");
  Require(dropped.Value().dropped_sequence == 10U, "dropped sequence changed");
  Require(drop_stream.Value().Poll().Value().sequence == 11U, "drop queue head changed");

  auto reject_stream = rds::RawDataStreamChannel::Open(Config());
  Require(reject_stream.HasValue(), "reject-newest stream did not open");
  Require(reject_stream.Value().Push(Frame(20U, {0x01U}, 300U)).HasValue(), "push 20 failed");
  Require(reject_stream.Value().Push(Frame(21U, {0x02U}, 301U)).HasValue(), "push 21 failed");
  auto rejected = reject_stream.Value().Push(Frame(22U, {0x03U}, 302U));
  Require(rejected.HasValue(), "reject-newest push failed");
  Require(
    rejected.Value().status == rds::ReceiptStatus::kBackpressureRejected,
    "reject-newest policy accepted full queue");

  const auto snapshot = stream.Value().Snapshot();
  Require(snapshot.state == rds::StreamState::kDegraded, "degraded stream state not reported");
  Require(snapshot.duplicate_frames == 1U, "duplicate count changed");
  Require(snapshot.stale_frames == 1U, "stale count changed");
  Require(snapshot.corrupt_frames == 1U, "corrupt count changed");

  Require(
    rds::ToString(rds::BufferPolicy::kDropOldest) == std::string_view("DropOldest"),
    "buffer policy text changed");
  Require(
    rds::ToString(rds::ReceiptStatus::kReassembled) == std::string_view("Reassembled"),
    "receipt status text changed");
  Require(
    rds::ToString(rds::StreamState::kReady) == std::string_view("Ready"),
    "stream state text changed");

  return 0;
}
