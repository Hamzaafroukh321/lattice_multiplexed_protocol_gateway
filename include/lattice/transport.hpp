#pragma once

#include "lattice/types.hpp"

#include <deque>
#include <optional>

namespace lattice {

class MemoryTransport {
 public:
  void write(Bytes bytes);
  [[nodiscard]] std::optional<Bytes> read();
  [[nodiscard]] std::size_t queued_bytes() const { return queued_bytes_; }
  [[nodiscard]] bool empty() const { return queue_.empty(); }
  void close() { closed_ = true; }
  [[nodiscard]] bool closed() const { return closed_; }

 private:
  std::deque<Bytes> queue_;
  std::size_t queued_bytes_{0};
  bool closed_{false};
};

struct MemoryPipe {
  MemoryTransport left_to_right;
  MemoryTransport right_to_left;
};

}  // namespace lattice
