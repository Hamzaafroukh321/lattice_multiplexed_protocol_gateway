#include "lattice/replay.hpp"

#include "lattice/frame.hpp"

#include <algorithm>
#include <limits>

namespace lattice {
namespace {

void append_u16(Bytes& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                    static_cast<std::uint16_t>(bytes[offset + 1U]));
}

}  // namespace

ReplayWindow::ReplayWindow(std::size_t capacity) : capacity_(capacity) {}

Result<void> ReplayWindow::record(std::uint16_t epoch, std::uint32_t frame_seq, Bytes encoded) {
  if (epoch != epoch_) {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "replay epoch mismatch");
  }
  if (contains(frame_seq)) {
    return make_error(ErrorScope::connection, ErrorCode::sequence_error,
                      CloseAction::close_connection, "duplicate replay frame sequence");
  }
  if (entries_.size() >= capacity_) {
    entries_.pop_front();
  }
  entries_.push_back(ReplayEntry{epoch, frame_seq, std::move(encoded), 0U});
  return {};
}

Result<std::vector<ReplayEntry>> ReplayWindow::acknowledge(std::uint16_t epoch,
                                                           std::vector<AckRange> ranges) {
  if (epoch != epoch_) {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "ACK epoch mismatch");
  }
  std::sort(ranges.begin(), ranges.end(),
            [](const AckRange& a, const AckRange& b) { return a.first < b.first; });
  for (std::size_t i = 0; i < ranges.size(); ++i) {
    if (ranges[i].first > ranges[i].last) {
      return make_error(ErrorScope::connection, ErrorCode::sequence_error,
                        CloseAction::close_connection, "invalid ACK range");
    }
    if (i > 0U && ranges[i - 1U].last >= ranges[i].first) {
      return make_error(ErrorScope::connection, ErrorCode::sequence_error,
                        CloseAction::close_connection, "overlapping ACK ranges");
    }
    const auto latest = latest_sequence();
    if (latest.has_value() && ranges[i].last > latest.value()) {
      return make_error(ErrorScope::connection, ErrorCode::sequence_error,
                        CloseAction::close_connection, "ACK range refers to unsent future frame");
    }
  }
  std::vector<ReplayEntry> retired;
  for (auto it = entries_.begin(); it != entries_.end();) {
    const bool acked = std::any_of(ranges.begin(), ranges.end(), [it](const AckRange& range) {
      return it->frame_seq >= range.first && it->frame_seq <= range.last;
    });
    if (acked) {
      retired.push_back(std::move(*it));
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
  return retired;
}

Result<std::vector<ReplayEntry>> ReplayWindow::due_for_retry(std::uint8_t retry_limit) {
  std::vector<ReplayEntry> due;
  for (ReplayEntry& entry : entries_) {
    if (entry.retries >= retry_limit) {
      return make_error(ErrorScope::channel, ErrorCode::sequence_error,
                        CloseAction::reset_channel, "replay retry limit exhausted");
    }
    entry.retries = static_cast<std::uint8_t>(entry.retries + 1U);
    due.push_back(entry);
  }
  return due;
}

Result<void> ReplayWindow::can_resume(const ResumeProof& proof) const {
  if (proof.epoch != epoch_) {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "resume epoch mismatch");
  }
  const auto earliest = earliest_sequence();
  const auto latest = latest_sequence();
  if (!earliest.has_value() || !latest.has_value()) {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "resume requested with empty replay window");
  }
  const std::uint32_t latest_plus_one =
      latest.value() == std::numeric_limits<std::uint32_t>::max()
          ? latest.value()
          : latest.value() + 1U;
  if (proof.first_required_seq < earliest.value() || proof.first_required_seq > latest_plus_one) {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "resume range is outside retained replay window");
  }
  return {};
}

bool ReplayWindow::contains(std::uint32_t frame_seq) const {
  return std::any_of(entries_.begin(), entries_.end(),
                     [frame_seq](const ReplayEntry& entry) { return entry.frame_seq == frame_seq; });
}

std::optional<std::uint32_t> ReplayWindow::earliest_sequence() const {
  if (entries_.empty()) {
    return std::nullopt;
  }
  return entries_.front().frame_seq;
}

std::optional<std::uint32_t> ReplayWindow::latest_sequence() const {
  if (entries_.empty()) {
    return std::nullopt;
  }
  return entries_.back().frame_seq;
}

TimerId TimerWheel::schedule(TimerKind kind, ChannelId channel, std::uint64_t due_ms) {
  TimerEvent event;
  event.id = TimerId{next_id_++};
  event.kind = kind;
  event.channel = channel;
  event.due_ms = due_ms;
  timers_.push_back(event);
  std::stable_sort(timers_.begin(), timers_.end(), [](const TimerEvent& a, const TimerEvent& b) {
    if (a.due_ms == b.due_ms) {
      return a.id.value < b.id.value;
    }
    return a.due_ms < b.due_ms;
  });
  return event.id;
}

Result<void> TimerWheel::cancel(TimerId id) {
  const auto old_size = timers_.size();
  timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                               [id](const TimerEvent& event) { return event.id == id; }),
                timers_.end());
  if (timers_.size() == old_size) {
    return make_error(ErrorScope::internal, ErrorCode::illegal_state, CloseAction::none,
                      "timer id is not live");
  }
  return {};
}

std::vector<TimerEvent> TimerWheel::expire(std::uint64_t now_ms) {
  std::vector<TimerEvent> expired;
  while (!timers_.empty() && timers_.front().due_ms <= now_ms) {
    expired.push_back(timers_.front());
    timers_.erase(timers_.begin());
  }
  return expired;
}

Result<Bytes> encode_ack_payload(std::uint16_t epoch, std::vector<AckRange> ranges) {
  std::sort(ranges.begin(), ranges.end(),
            [](const AckRange& a, const AckRange& b) { return a.first < b.first; });
  for (std::size_t i = 0; i < ranges.size(); ++i) {
    if (ranges[i].first > ranges[i].last) {
      return make_error(ErrorScope::connection, ErrorCode::sequence_error,
                        CloseAction::close_connection, "invalid ACK range");
    }
    if (i > 0U && ranges[i - 1U].last >= ranges[i].first) {
      return make_error(ErrorScope::connection, ErrorCode::sequence_error,
                        CloseAction::close_connection, "overlapping ACK ranges");
    }
  }
  if (ranges.size() > 0xFFFFU) {
    return make_error(ErrorScope::connection, ErrorCode::resource_limit,
                      CloseAction::close_connection, "too many ACK ranges");
  }
  Bytes out;
  append_u16(out, epoch);
  append_u16(out, static_cast<std::uint16_t>(ranges.size()));
  for (const AckRange& range : ranges) {
    append_u32(out, range.first);
    append_u32(out, range.last);
  }
  return out;
}

Result<std::pair<std::uint16_t, std::vector<AckRange>>> decode_ack_payload(
    std::span<const std::uint8_t> payload) {
  if (payload.size() < 4U) {
    return make_error(ErrorScope::connection, ErrorCode::sequence_error,
                      CloseAction::close_connection, "ACK payload too short");
  }
  const std::uint16_t epoch = read_u16(payload, 0U);
  const std::uint16_t count = read_u16(payload, 2U);
  const std::size_t expected = 4U + static_cast<std::size_t>(count) * 8U;
  if (payload.size() != expected) {
    return make_error(ErrorScope::connection, ErrorCode::sequence_error,
                      CloseAction::close_connection, "ACK payload length mismatch");
  }
  std::vector<AckRange> ranges;
  ranges.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t offset = 4U + i * 8U;
    auto first = read_u32_be(payload, offset);
    auto last = read_u32_be(payload, offset + 4U);
    if (!first || !last) {
      return make_error(ErrorScope::connection, ErrorCode::sequence_error,
                        CloseAction::close_connection, "ACK range is truncated");
    }
    ranges.push_back(AckRange{first.value(), last.value()});
  }
  auto canonical = encode_ack_payload(epoch, ranges);
  if (!canonical) {
    return canonical.error();
  }
  return std::make_pair(epoch, ranges);
}

}  // namespace lattice
