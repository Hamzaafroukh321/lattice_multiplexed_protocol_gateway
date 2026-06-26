#include "lattice/scheduler.hpp"

#include <algorithm>

namespace lattice {

OutboundScheduler::OutboundScheduler(std::size_t byte_limit) : byte_limit_(byte_limit) {}

Result<void> OutboundScheduler::enqueue(OutboundItem item) {
  if (item.encoded.empty()) {
    return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                      "scheduler item cannot be empty");
  }
  std::size_t next = 0;
  if (!checked_add(queued_bytes_, item.encoded.size(), &next) || next > byte_limit_) {
    return make_error(ErrorScope::connection, ErrorCode::would_block, CloseAction::none,
                      "outbound scheduler byte limit reached");
  }
  queued_bytes_ = next;
  if (item.priority == OutboundPriority::control) {
    control_.push_back(std::move(item));
    return {};
  }
  auto& queue = data_[item.channel.number];
  if (!queue.empty() && item.sequence <= queue.back().sequence) {
    queued_bytes_ -= item.encoded.size();
    return make_error(ErrorScope::channel, ErrorCode::sequence_error,
                      CloseAction::reset_channel, "data scheduler sequence regressed");
  }
  queue.push_back(std::move(item));
  return {};
}

std::vector<OutboundItem> OutboundScheduler::drain(std::size_t writable_bytes) {
  std::vector<OutboundItem> out;
  std::size_t used = 0;
  while (!control_.empty() && used + control_.front().encoded.size() <= writable_bytes) {
    used += control_.front().encoded.size();
    queued_bytes_ -= control_.front().encoded.size();
    out.push_back(std::move(control_.front()));
    control_.pop_front();
  }
  if (used >= writable_bytes || data_.empty()) {
    return out;
  }

  std::vector<std::uint32_t> channels;
  channels.reserve(data_.size());
  for (const auto& [channel_no, queue] : data_) {
    if (!queue.empty()) {
      channels.push_back(channel_no);
    }
  }
  if (channels.empty()) {
    return out;
  }
  std::sort(channels.begin(), channels.end());
  auto start = std::lower_bound(channels.begin(), channels.end(), rr_cursor_);
  if (start == channels.end()) {
    start = channels.begin();
  }
  std::rotate(channels.begin(), start, channels.end());

  for (std::uint32_t channel_no : channels) {
    auto queue_it = data_.find(channel_no);
    if (queue_it == data_.end() || queue_it->second.empty()) {
      continue;
    }
    OutboundItem& item = queue_it->second.front();
    const std::uint32_t expected = expected_sequence_[channel_no];
    if (expected != 0U && item.sequence != expected) {
      continue;
    }
    if (used + item.encoded.size() > writable_bytes) {
      continue;
    }
    used += item.encoded.size();
    queued_bytes_ -= item.encoded.size();
    expected_sequence_[channel_no] = item.sequence + 1U;
    rr_cursor_ = channel_no + 1U;
    out.push_back(std::move(item));
    queue_it->second.pop_front();
  }
  for (auto it = data_.begin(); it != data_.end();) {
    if (it->second.empty()) {
      it = data_.erase(it);
    } else {
      ++it;
    }
  }
  return out;
}

bool OutboundScheduler::empty() const {
  if (!control_.empty()) {
    return false;
  }
  return std::all_of(data_.begin(), data_.end(),
                     [](const auto& entry) { return entry.second.empty(); });
}

}  // namespace lattice
