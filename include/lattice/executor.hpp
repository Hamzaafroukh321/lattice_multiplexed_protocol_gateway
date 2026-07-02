#pragma once

#include "lattice/error.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
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

class ConnectionShardRouter {
 public:
  explicit ConnectionShardRouter(DeterministicExecutor& executor);

  [[nodiscard]] std::uint32_t shard_for(std::uint64_t connection_id) const;
  [[nodiscard]] Result<std::uint64_t> submit(std::uint64_t connection_id,
                                             std::function<Result<void>()> task);

 private:
  DeterministicExecutor& executor_;
};

class ThreadedExecutor {
 public:
  explicit ThreadedExecutor(std::size_t capacity, std::uint32_t shards = 1);
  ThreadedExecutor(const ThreadedExecutor&) = delete;
  ThreadedExecutor& operator=(const ThreadedExecutor&) = delete;
  ~ThreadedExecutor();

  [[nodiscard]] Result<std::uint64_t> submit(std::uint32_t shard,
                                             std::function<Result<void>()> task);
  [[nodiscard]] Result<void> wait_idle();
  [[nodiscard]] Result<void> shutdown();
  [[nodiscard]] std::size_t queued() const;
  [[nodiscard]] std::uint32_t shards() const { return shard_count_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::uint32_t shard_count_{1};
};

}  // namespace lattice
