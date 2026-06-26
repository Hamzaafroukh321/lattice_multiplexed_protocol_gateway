#pragma once

#include "lattice/error.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace lattice {

struct ExecutorTask {
  std::uint64_t id{0};
  std::uint32_t shard{0};
  std::function<Result<void>()> run;
};

class DeterministicExecutor {
 public:
  explicit DeterministicExecutor(std::size_t capacity, std::uint32_t shards = 1);

  [[nodiscard]] Result<std::uint64_t> submit(std::uint32_t shard, std::function<Result<void>()> task);
  [[nodiscard]] Result<void> cancel(std::uint64_t id);
  [[nodiscard]] Result<void> drain_one();
  [[nodiscard]] Result<void> drain_all();
  [[nodiscard]] std::size_t queued() const { return queue_.size(); }
  [[nodiscard]] std::uint32_t shards() const { return shards_; }

 private:
  std::size_t capacity_{0};
  std::uint32_t shards_{1};
  std::uint64_t next_id_{1};
  std::deque<ExecutorTask> queue_;
};

}  // namespace lattice
