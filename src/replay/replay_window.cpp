#include "lattice/replay.hpp"

#include <algorithm>

namespace lattice {

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

bool ReplayWindow::contains(std::uint32_t frame_seq) const {
  return std::any_of(entries_.begin(), entries_.end(),
                     [frame_seq](const ReplayEntry& entry) { return entry.frame_seq == frame_seq; });
}

}  // namespace lattice
