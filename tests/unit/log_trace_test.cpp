// SPDX-License-Identifier: MIT

#include "openautosar/log/logger.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
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

openautosar::log::LoggerConfig TestConfig(
  openautosar::log::BufferPolicy policy = openautosar::log::BufferPolicy::kDropOldest) {
  return {
    .application_id = "oa-test-app",
    .context_id = "LTST",
    .process_identity = "log-trace-test",
    .machine_id = "qemux86-64",
    .software_cluster_version = "0.1.0-test",
    .buffer_capacity = 2U,
    .max_message_size = 8U,
    .buffer_policy = policy,
    .rate_limit = {
      .max_records = 2U,
      .window = std::chrono::milliseconds{100},
    },
  };
}

}  // namespace

int main() {
  namespace log = openautosar::log;
  using Steady = std::chrono::steady_clock;
  using Wall = std::chrono::system_clock;

  std::vector<log::Record> delivered;
  log::Logger logger("communication.local-ipc", TestConfig());
  logger.SetSink([&delivered](const log::Record& record) { delivered.push_back(record); });

  const auto monotonic_zero = Steady::time_point{};
  const auto wall_zero = Wall::time_point{};
  logger.LogAt(
    log::Level::kInfo,
    "first",
    monotonic_zero,
    wall_zero,
    {{.key = "service", .value = "UltrasonicDistanceService"}},
    "corr-1");
  logger.LogAt(
    log::Level::kWarn,
    "second",
    monotonic_zero + std::chrono::milliseconds{1},
    wall_zero + std::chrono::milliseconds{1});
  logger.LogAt(
    log::Level::kError,
    "third",
    monotonic_zero + std::chrono::milliseconds{2},
    wall_zero + std::chrono::milliseconds{2});

  auto stats = logger.Statistics();
  Require(stats.accepted_records == 3U, "accepted log count changed");
  Require(stats.delivered_records == 2U, "rate-limited log was delivered");
  Require(stats.dropped_by_rate_limit == 1U, "rate-limit loss was not counted");
  Require(delivered.size() == 2U, "sink delivery count changed");
  Require(delivered[0U].application_id == "oa-test-app", "application id not propagated");
  Require(delivered[0U].context_id == "LTST", "context id not propagated");
  Require(delivered[0U].process_identity == "log-trace-test", "process identity changed");
  Require(!delivered[0U].thread_identity.empty(), "thread identity missing");
  Require(delivered[0U].machine_id == "qemux86-64", "machine id changed");
  Require(delivered[0U].software_cluster_version == "0.1.0-test", "cluster version changed");
  Require(delivered[0U].correlation_id == "corr-1", "correlation id changed");
  Require(delivered[0U].fields.size() == 1U, "structured field count changed");
  Require(delivered[0U].wall_timestamp_valid, "wall timestamp not marked valid");
  Require(delivered[0U].monotonic_timestamp == monotonic_zero, "monotonic timestamp changed");

  logger.LogAt(
    log::Level::kInfo,
    "message-longer-than-limit",
    monotonic_zero + std::chrono::milliseconds{150},
    std::nullopt);
  stats = logger.Statistics();
  Require(stats.delivered_records == 3U, "post-window log was not delivered");
  Require(stats.truncated_messages == 1U, "message truncation was not counted");
  Require(stats.dropped_by_buffer_limit == 1U, "drop-oldest buffer loss was not counted");
  Require(delivered.back().message == "message-", "truncated message text changed");
  Require(!delivered.back().wall_timestamp_valid, "missing wall timestamp marked valid");

  const auto buffered = logger.BufferedRecords();
  Require(buffered.size() == 2U, "bounded log buffer size changed");
  Require(buffered[0U].message == "second", "drop-oldest buffer did not preserve second record");
  Require(buffered[1U].message == "message-", "drop-oldest buffer did not store newest record");

  logger.ClearBuffer();
  Require(logger.BufferedRecords().empty(), "clear buffer failed");
  logger.ResetStatistics();
  Require(logger.Statistics().accepted_records == 0U, "statistics reset failed");

  log::Logger reject_logger("reject-buffer", TestConfig(log::BufferPolicy::kRejectNewest));
  reject_logger.SetSink([](const log::Record&) {});
  reject_logger.LogAt(log::Level::kInfo, "one", monotonic_zero, wall_zero);
  reject_logger.LogAt(
    log::Level::kInfo,
    "two",
    monotonic_zero + std::chrono::milliseconds{1},
    wall_zero);
  reject_logger.LogAt(
    log::Level::kInfo,
    "three",
    monotonic_zero + std::chrono::milliseconds{150},
    wall_zero);
  const auto reject_buffer = reject_logger.BufferedRecords();
  Require(reject_buffer.size() == 2U, "reject-newest buffer size changed");
  Require(reject_buffer[0U].message == "one", "reject-newest dropped oldest record");
  Require(reject_buffer[1U].message == "two", "reject-newest did not preserve second record");
  Require(
    reject_logger.Statistics().dropped_by_buffer_limit == 1U,
    "reject-newest buffer loss was not counted");

  log::Logger throwing_logger("throwing-sink", TestConfig());
  throwing_logger.SetSink([](const log::Record&) { throw std::runtime_error("sink failed"); });
  throwing_logger.Info("failure");
  Require(throwing_logger.Statistics().sink_failures == 1U, "sink failure was not counted");
  Require(throwing_logger.Statistics().delivered_records == 0U, "failed sink counted delivered");

  Require(log::ToString(log::Level::kWarn) == std::string_view("warn"), "level text changed");
  Require(
    log::ToString(log::BufferPolicy::kRejectNewest) == std::string_view("reject-newest"),
    "buffer policy text changed");

  return 0;
}
