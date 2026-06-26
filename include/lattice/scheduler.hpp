#pragma once

#include "lattice/error.hpp"
#include "lattice/types.hpp"

#include <deque>
#include <map>

namespace lattice {

enum class OutboundPriority : std::uint8_t {
  control,
  data
};

struct OutboundItem {
  OutboundPriority priority{OutboundPriority::data};
  ChannelId channel;
  std::uint32_t sequence{0};
  Bytes encoded;
};

class OutboundScheduler {
 public:
  explicit OutboundScheduler(std::size_t byte_limit);

  [[nodiscard]] Result<void> enqueue(OutboundItem item);
  [[nodiscard]] std::vector<OutboundItem> drain(std::size_t writable_bytes);
  [[nodiscard]] std::size_t queued_bytes() const { return queued_bytes_; }
  [[nodiscard]] bool empty() const;

 private:
  std::size_t byte_limit_{0};
  std::size_t queued_bytes_{0};
  std::deque<OutboundItem> control_;
  std::map<std::uint32_t, std::deque<OutboundItem>> data_;
  std::map<std::uint32_t, std::uint32_t> expected_sequence_;
  std::uint32_t rr_cursor_{0};
};

}  // namespace lattice
