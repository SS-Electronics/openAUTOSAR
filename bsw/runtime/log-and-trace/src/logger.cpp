// SPDX-License-Identifier: MIT

#include "openautosar/log/logger.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

namespace openautosar::log {
namespace {

void DefaultSink(const Record& record) {
  std::cerr << '[' << ToString(record.level) << "] " << record.component << ": " << record.message
            << '\n';
}

[[nodiscard]] std::string ThreadIdentity() {
  std::ostringstream output;
  output << std::this_thread::get_id();
  return output.str();
}

[[nodiscard]] std::string Truncate(
  std::string_view message,
  std::size_t max_size,
  LogStatistics& statistics) {
  if (max_size == 0U || message.size() <= max_size) {
    return std::string(message);
  }

  ++statistics.truncated_messages;
  return std::string(message.substr(0U, max_size));
}

}  // namespace

Logger::Logger(std::string component) : Logger(std::move(component), {}) {}

Logger::Logger(std::string component, LoggerConfig config)
  : component_(std::move(component)), config_(std::move(config)) {}

void Logger::Configure(LoggerConfig config) {
  std::lock_guard lock(mutex_);
  config_ = std::move(config);
  buffer_.clear();
  rate_window_start_ = {};
  rate_window_count_ = 0U;
  statistics_ = {};
}

void Logger::SetSink(Sink sink) {
  std::lock_guard lock(mutex_);
  sink_ = std::move(sink);
}

void Logger::Debug(std::string_view message) { Log(Level::kDebug, message); }

void Logger::Debug(
  std::string_view message,
  std::vector<Field> fields,
  std::string correlation_id) {
  Log(Level::kDebug, message, std::move(fields), std::move(correlation_id));
}

void Logger::Info(std::string_view message) { Log(Level::kInfo, message); }

void Logger::Info(
  std::string_view message,
  std::vector<Field> fields,
  std::string correlation_id) {
  Log(Level::kInfo, message, std::move(fields), std::move(correlation_id));
}

void Logger::Warn(std::string_view message) { Log(Level::kWarn, message); }

void Logger::Warn(
  std::string_view message,
  std::vector<Field> fields,
  std::string correlation_id) {
  Log(Level::kWarn, message, std::move(fields), std::move(correlation_id));
}

void Logger::Error(std::string_view message) { Log(Level::kError, message); }

void Logger::Error(
  std::string_view message,
  std::vector<Field> fields,
  std::string correlation_id) {
  Log(Level::kError, message, std::move(fields), std::move(correlation_id));
}

void Logger::Log(
  Level level,
  std::string_view message,
  std::vector<Field> fields,
  std::string correlation_id) {
  LogAt(
    level,
    message,
    std::chrono::steady_clock::now(),
    std::chrono::system_clock::now(),
    std::move(fields),
    std::move(correlation_id));
}

void Logger::LogAt(
  Level level,
  std::string_view message,
  std::chrono::steady_clock::time_point monotonic_timestamp,
  std::optional<std::chrono::system_clock::time_point> wall_timestamp,
  std::vector<Field> fields,
  std::string correlation_id) {
  Write(
    level,
    message,
    monotonic_timestamp,
    wall_timestamp,
    std::move(fields),
    std::move(correlation_id));
}

LogStatistics Logger::Statistics() const {
  std::lock_guard lock(mutex_);
  return statistics_;
}

std::vector<Record> Logger::BufferedRecords() const {
  std::lock_guard lock(mutex_);
  return {buffer_.begin(), buffer_.end()};
}

void Logger::ClearBuffer() {
  std::lock_guard lock(mutex_);
  buffer_.clear();
}

void Logger::ResetStatistics() {
  std::lock_guard lock(mutex_);
  statistics_ = {};
  rate_window_start_ = {};
  rate_window_count_ = 0U;
}

void Logger::Write(
  Level level,
  std::string_view message,
  std::chrono::steady_clock::time_point monotonic_timestamp,
  std::optional<std::chrono::system_clock::time_point> wall_timestamp,
  std::vector<Field> fields,
  std::string correlation_id) {
  Sink sink;
  Record record;
  {
    std::lock_guard lock(mutex_);
    ++statistics_.accepted_records;
    if (!AllowByRateLimit(monotonic_timestamp)) {
      ++statistics_.dropped_by_rate_limit;
      return;
    }

    record = {
      .level = level,
      .component = component_,
      .message = Truncate(message, config_.max_message_size, statistics_),
      .monotonic_timestamp = monotonic_timestamp,
      .wall_timestamp = wall_timestamp.value_or(std::chrono::system_clock::time_point{}),
      .wall_timestamp_valid = wall_timestamp.has_value(),
      .application_id = config_.application_id,
      .context_id = config_.context_id,
      .process_identity = config_.process_identity,
      .thread_identity = ThreadIdentity(),
      .machine_id = config_.machine_id,
      .software_cluster_version = config_.software_cluster_version,
      .correlation_id = std::move(correlation_id),
      .fields = std::move(fields),
    };
    StoreBufferedRecord(record);
    sink = sink_;
  }

  try {
    if (sink) {
      sink(record);
    } else {
      DefaultSink(record);
    }
    std::lock_guard lock(mutex_);
    ++statistics_.delivered_records;
  } catch (...) {
    std::lock_guard lock(mutex_);
    ++statistics_.sink_failures;
  }
}

bool Logger::AllowByRateLimit(
  std::chrono::steady_clock::time_point monotonic_timestamp) {
  if (config_.rate_limit.max_records == 0U || config_.rate_limit.window.count() <= 0) {
    return true;
  }

  if (rate_window_count_ == 0U ||
      monotonic_timestamp - rate_window_start_ >= config_.rate_limit.window) {
    rate_window_start_ = monotonic_timestamp;
    rate_window_count_ = 0U;
  }

  if (rate_window_count_ >= config_.rate_limit.max_records) {
    return false;
  }

  ++rate_window_count_;
  return true;
}

void Logger::StoreBufferedRecord(const Record& record) {
  if (config_.buffer_capacity == 0U) {
    return;
  }

  if (buffer_.size() >= config_.buffer_capacity) {
    ++statistics_.dropped_by_buffer_limit;
    if (config_.buffer_policy == BufferPolicy::kRejectNewest) {
      return;
    }
    buffer_.pop_front();
  }

  buffer_.push_back(record);
}

std::string_view ToString(Level level) noexcept {
  switch (level) {
    case Level::kDebug:
      return "debug";
    case Level::kInfo:
      return "info";
    case Level::kWarn:
      return "warn";
    case Level::kError:
      return "error";
  }

  return "unknown";
}

std::string_view ToString(BufferPolicy policy) noexcept {
  switch (policy) {
    case BufferPolicy::kDropOldest:
      return "drop-oldest";
    case BufferPolicy::kRejectNewest:
      return "reject-newest";
  }

  return "unknown";
}

}  // namespace openautosar::log
