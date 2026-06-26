#include "lattice/transport.hpp"

namespace lattice {

void MemoryTransport::write(Bytes bytes) {
  queued_bytes_ += bytes.size();
  queue_.push_back(std::move(bytes));
}

std::optional<Bytes> MemoryTransport::read() {
  if (queue_.empty()) {
    return std::nullopt;
  }
  Bytes bytes = std::move(queue_.front());
  queue_.pop_front();
  queued_bytes_ -= bytes.size();
  return bytes;
}

}  // namespace lattice
