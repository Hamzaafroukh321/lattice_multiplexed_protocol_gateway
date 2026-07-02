#include "lattice/replay.hpp"

#include "lattice/frame.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

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

[[nodiscard]] std::string hex(const Bytes& bytes) {
  std::ostringstream out;
  for (std::uint8_t byte : bytes) {
    out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
  }
  return out.str();
}

[[nodiscard]] Result<Bytes> parse_hex(const std::string& text) {
  if ((text.size() % 2U) != 0U) {
    return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                      "replay snapshot hex has odd length");
  }
  Bytes out;
  out.reserve(text.size() / 2U);
  const auto nibble = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
      return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
      return 10 + ch - 'A';
    }
    return -1;
  };
  for (std::size_t i = 0; i < text.size(); i += 2U) {
    const int high = nibble(text[i]);
    const int low = nibble(text[i + 1U]);
    if (high < 0 || low < 0) {
      return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                        "replay snapshot hex contains non-hex byte");
    }
    out.push_back(static_cast<std::uint8_t>((high << 4) | low));
  }
  return out;
}

[[nodiscard]] Result<std::uint64_t> parse_u64(const std::string& text) {
  if (text.empty()) {
    return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                      "replay snapshot integer is empty");
  }
  std::uint64_t value = 0;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                        "replay snapshot integer is not decimal");
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
      return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                        "replay snapshot integer overflows");
    }
    value = value * 10U + digit;
  }
  return value;
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

Result<std::vector<ReplayEntry>> ReplayWindow::retained_from(
    std::uint16_t epoch, std::uint32_t first_required_seq) const {
  ResumeProof proof;
  proof.epoch = epoch;
  proof.first_required_seq = first_required_seq;
  auto resumable = can_resume(proof);
  if (!resumable) {
    return resumable.error();
  }
  std::vector<ReplayEntry> retained;
  for (const ReplayEntry& entry : entries_) {
    if (entry.frame_seq >= first_required_seq) {
      retained.push_back(entry);
    }
  }
  return retained;
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

Result<std::string> ReplayWindow::serialize_retained() const {
  std::ostringstream out;
  out << "LTXREPLAY/1\n";
  out << "capacity|" << capacity_ << '\n';
  out << "epoch|" << epoch_ << '\n';
  for (const ReplayEntry& entry : entries_) {
    out << "entry|" << entry.frame_seq << '|' << static_cast<unsigned>(entry.retries)
        << '|' << hex(entry.encoded) << '\n';
  }
  return out.str();
}

Result<ReplayWindow> ReplayWindow::restore_retained(const std::string& text) {
  std::istringstream in(text);
  std::string line;
  if (!std::getline(in, line) || line != "LTXREPLAY/1") {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "replay snapshot header mismatch");
  }
  if (!std::getline(in, line) || line.rfind("capacity|", 0U) != 0U) {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "replay snapshot missing capacity");
  }
  auto capacity = parse_u64(line.substr(9U));
  if (!capacity || capacity.value() == 0U ||
      capacity.value() > std::numeric_limits<std::size_t>::max()) {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "replay snapshot capacity is invalid");
  }
  if (!std::getline(in, line) || line.rfind("epoch|", 0U) != 0U) {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "replay snapshot missing epoch");
  }
  auto epoch = parse_u64(line.substr(6U));
  if (!epoch || epoch.value() == 0U || epoch.value() > 0xFFFFU) {
    return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                      CloseAction::close_connection, "replay snapshot epoch is invalid");
  }
  ReplayWindow restored(static_cast<std::size_t>(capacity.value()));
  restored.epoch_ = static_cast<std::uint16_t>(epoch.value());
  std::optional<std::uint32_t> previous_seq;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    const std::size_t first = line.find('|');
    const std::size_t second = first == std::string::npos ? std::string::npos : line.find('|', first + 1U);
    const std::size_t third = second == std::string::npos ? std::string::npos : line.find('|', second + 1U);
    if (first == std::string::npos || second == std::string::npos || third == std::string::npos ||
        line.substr(0U, first) != "entry") {
      return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                        CloseAction::close_connection, "malformed replay snapshot entry");
    }
    auto seq = parse_u64(line.substr(first + 1U, second - first - 1U));
    auto retries = parse_u64(line.substr(second + 1U, third - second - 1U));
    auto encoded = parse_hex(line.substr(third + 1U));
    if (!seq || !retries || !encoded || seq.value() > 0xFFFFFFFFULL || retries.value() > 0xFFU ||
        encoded.value().empty()) {
      return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                        CloseAction::close_connection, "replay snapshot entry is invalid");
    }
    const std::uint32_t frame_seq = static_cast<std::uint32_t>(seq.value());
    if (previous_seq.has_value() && previous_seq.value() >= frame_seq) {
      return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                        CloseAction::close_connection, "replay snapshot entries are not ordered");
    }
    if (restored.entries_.size() >= restored.capacity_) {
      return make_error(ErrorScope::connection, ErrorCode::resume_rejected,
                        CloseAction::close_connection, "replay snapshot exceeds capacity");
    }
    previous_seq = frame_seq;
    restored.entries_.push_back(ReplayEntry{restored.epoch_, frame_seq, encoded.take_value(),
                                            static_cast<std::uint8_t>(retries.value())});
  }
  return restored;
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
