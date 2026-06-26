#include "lattice/executor.hpp"

#include <algorithm>

namespace lattice {

DeterministicExecutor::DeterministicExecutor(std::size_t capacity, std::uint32_t shards)
    : capacity_(capacity), shards_(shards == 0U ? 1U : shards) {}

Result<std::uint64_t> DeterministicExecutor::submit(std::uint32_t shard,
                                                    std::function<Result<void>()> task) {
  if (!task) {
    return make_error(ErrorScope::internal, ErrorCode::illegal_state, CloseAction::none,
                      "executor task is empty");
  }
  if (shard >= shards_) {
    return make_error(ErrorScope::internal, ErrorCode::resource_limit, CloseAction::none,
                      "executor shard is out of range");
  }
  if (queue_.size() >= capacity_) {
    return make_error(ErrorScope::internal, ErrorCode::would_block, CloseAction::none,
                      "executor queue is full");
  }
  const std::uint64_t id = next_id_++;
  queue_.push_back(ExecutorTask{id, shard, std::move(task)});
  return id;
}

Result<void> DeterministicExecutor::cancel(std::uint64_t id) {
  const auto old_size = queue_.size();
  queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                              [id](const ExecutorTask& task) { return task.id == id; }),
               queue_.end());
  if (queue_.size() == old_size) {
    return make_error(ErrorScope::internal, ErrorCode::illegal_state, CloseAction::none,
                      "executor task is not queued");
  }
  return {};
}

Result<void> DeterministicExecutor::drain_one() {
  if (queue_.empty()) {
    return {};
  }
  ExecutorTask task = std::move(queue_.front());
  queue_.pop_front();
  return task.run();
}

Result<void> DeterministicExecutor::drain_all() {
  while (!queue_.empty()) {
    auto drained = drain_one();
    if (!drained) {
      return drained.error();
    }
  }
  return {};
}

ConnectionShardRouter::ConnectionShardRouter(DeterministicExecutor& executor)
    : executor_(executor) {}

std::uint32_t ConnectionShardRouter::shard_for(std::uint64_t connection_id) const {
  const std::uint32_t shard_count = executor_.shards() == 0U ? 1U : executor_.shards();
  return static_cast<std::uint32_t>(connection_id % shard_count);
}

Result<std::uint64_t> ConnectionShardRouter::submit(
    std::uint64_t connection_id, std::function<Result<void>()> task) {
  return executor_.submit(shard_for(connection_id), std::move(task));
}

}  // namespace lattice
