// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openautosar::log {

enum class Level { kDebug, kInfo, kWarn, kError };

enum class BufferPolicy {
  kDropOldest,
  kRejectNewest,
};

struct Field final {
  std::string key;
  std::string value;

  friend bool operator==(const Field&, const Field&) = default;
};

struct RateLimitConfig final {
  std::uint32_t max_records{1'000U};
  std::chrono::milliseconds window{1'000};
};

struct LoggerConfig final {
  std::string application_id{"openautosar"};
  std::string context_id{"default"};
  std::string process_identity{"host-process"};
  std::string machine_id{"qemux86-64"};
  std::string software_cluster_version{"0.1.0"};
  std::size_t buffer_capacity{128U};
  std::size_t max_message_size{4U * 1024U};
  BufferPolicy buffer_policy{BufferPolicy::kDropOldest};
  RateLimitConfig rate_limit{};
};

struct Record final {
  Level level{Level::kInfo};
  std::string component;
  std::string message;
  std::chrono::steady_clock::time_point monotonic_timestamp{};
  std::chrono::system_clock::time_point wall_timestamp{};
  bool wall_timestamp_valid{false};
  std::string application_id;
  std::string context_id;
  std::string process_identity;
  std::string thread_identity;
  std::string machine_id;
  std::string software_cluster_version;
  std::string correlation_id;
  std::vector<Field> fields;
};

struct LogStatistics final {
  std::uint64_t accepted_records{0U};
  std::uint64_t delivered_records{0U};
  std::uint64_t dropped_by_rate_limit{0U};
  std::uint64_t dropped_by_buffer_limit{0U};
  std::uint64_t truncated_messages{0U};
  std::uint64_t sink_failures{0U};
};

class Logger final {
public:
  using Sink = std::function<void(const Record&)>;

  explicit Logger(std::string component);
  Logger(std::string component, LoggerConfig config);

  void Configure(LoggerConfig config);
  void SetSink(Sink sink);

  void Debug(std::string_view message);
  void Debug(
    std::string_view message,
    std::vector<Field> fields,
    std::string correlation_id = {});
  void Info(std::string_view message);
  void Info(
    std::string_view message,
    std::vector<Field> fields,
    std::string correlation_id = {});
  void Warn(std::string_view message);
  void Warn(
    std::string_view message,
    std::vector<Field> fields,
    std::string correlation_id = {});
  void Error(std::string_view message);
  void Error(
    std::string_view message,
    std::vector<Field> fields,
    std::string correlation_id = {});

  void Log(
    Level level,
    std::string_view message,
    std::vector<Field> fields = {},
    std::string correlation_id = {});

  void LogAt(
    Level level,
    std::string_view message,
    std::chrono::steady_clock::time_point monotonic_timestamp,
    std::optional<std::chrono::system_clock::time_point> wall_timestamp,
    std::vector<Field> fields = {},
    std::string correlation_id = {});

  [[nodiscard]] LogStatistics Statistics() const;
  [[nodiscard]] std::vector<Record> BufferedRecords() const;
  void ClearBuffer();
  void ResetStatistics();

private:
  void Write(
    Level level,
    std::string_view message,
    std::chrono::steady_clock::time_point monotonic_timestamp,
    std::optional<std::chrono::system_clock::time_point> wall_timestamp,
    std::vector<Field> fields,
    std::string correlation_id);
  [[nodiscard]] bool AllowByRateLimit(
    std::chrono::steady_clock::time_point monotonic_timestamp);
  void StoreBufferedRecord(const Record& record);

  std::string component_;
  LoggerConfig config_;
  Sink sink_;
  LogStatistics statistics_;
  std::deque<Record> buffer_;
  std::chrono::steady_clock::time_point rate_window_start_{};
  std::uint32_t rate_window_count_{0U};
  mutable std::mutex mutex_;
};

[[nodiscard]] std::string_view ToString(Level level) noexcept;
[[nodiscard]] std::string_view ToString(BufferPolicy policy) noexcept;

}  // namespace openautosar::log
