#pragma once

#include "lattice/error.hpp"
#include "lattice/types.hpp"

#include <array>
#include <deque>
#include <optional>
#include <span>
#include <utility>

namespace lattice {

struct ReplayEntry {
  std::uint16_t epoch{1};
  std::uint32_t frame_seq{0};
  Bytes encoded;
  std::uint8_t retries{0};
};

struct AckRange {
  std::uint32_t first{0};
  std::uint32_t last{0};
};

struct ResumeProof {
  std::array<std::uint8_t, 16> transcript_hash{};
  std::uint16_t epoch{1};
  std::uint32_t first_required_seq{0};
};

struct TimerId {
  std::uint64_t value{0};
  friend bool operator==(const TimerId&, const TimerId&) = default;
};

enum class TimerKind : std::uint8_t {
  handshake,
  fragment_gap,
  retry,
  idle,
  pong_deadline,
  drain
};

struct TimerEvent {
  TimerId id;
  TimerKind kind{TimerKind::idle};
  ChannelId channel;
  std::uint64_t due_ms{0};
};

class ReplayWindow {
 public:
  explicit ReplayWindow(std::size_t capacity = 4096);

  [[nodiscard]] Result<void> record(std::uint16_t epoch, std::uint32_t frame_seq, Bytes encoded);
  [[nodiscard]] Result<std::vector<ReplayEntry>> acknowledge(std::uint16_t epoch,
                                                             std::vector<AckRange> ranges);
  [[nodiscard]] Result<std::vector<ReplayEntry>> due_for_retry(std::uint8_t retry_limit);
  [[nodiscard]] Result<std::vector<ReplayEntry>> retained_from(std::uint16_t epoch,
                                                               std::uint32_t first_required_seq) const;
  [[nodiscard]] Result<void> can_resume(const ResumeProof& proof) const;
  [[nodiscard]] bool contains(std::uint32_t frame_seq) const;
  [[nodiscard]] std::optional<std::uint32_t> earliest_sequence() const;
  [[nodiscard]] std::optional<std::uint32_t> latest_sequence() const;
  [[nodiscard]] std::size_t size() const { return entries_.size(); }

 private:
  std::size_t capacity_;
  std::uint16_t epoch_{1};
  std::deque<ReplayEntry> entries_;
};

class VirtualClock {
 public:
  [[nodiscard]] std::uint64_t now_ms() const { return now_ms_; }
  void advance_ms(std::uint64_t delta) { now_ms_ += delta; }

 private:
  std::uint64_t now_ms_{0};
};

class TimerWheel {
 public:
  [[nodiscard]] TimerId schedule(TimerKind kind, ChannelId channel, std::uint64_t due_ms);
  [[nodiscard]] Result<void> cancel(TimerId id);
  [[nodiscard]] std::vector<TimerEvent> expire(std::uint64_t now_ms);
  [[nodiscard]] std::size_t size() const { return timers_.size(); }

 private:
  std::uint64_t next_id_{1};
  std::vector<TimerEvent> timers_;
};

[[nodiscard]] Result<Bytes> encode_ack_payload(std::uint16_t epoch,
                                               std::vector<AckRange> ranges);
[[nodiscard]] Result<std::pair<std::uint16_t, std::vector<AckRange>>> decode_ack_payload(
    std::span<const std::uint8_t> payload);

}  // namespace lattice
