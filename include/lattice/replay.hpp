#pragma once

#include "lattice/error.hpp"
#include "lattice/types.hpp"

#include <deque>
#include <set>

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

class ReplayWindow {
 public:
  explicit ReplayWindow(std::size_t capacity = 4096);

  [[nodiscard]] Result<void> record(std::uint16_t epoch, std::uint32_t frame_seq, Bytes encoded);
  [[nodiscard]] Result<std::vector<ReplayEntry>> acknowledge(std::uint16_t epoch,
                                                             std::vector<AckRange> ranges);
  [[nodiscard]] Result<std::vector<ReplayEntry>> due_for_retry(std::uint8_t retry_limit);
  [[nodiscard]] bool contains(std::uint32_t frame_seq) const;
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

}  // namespace lattice
