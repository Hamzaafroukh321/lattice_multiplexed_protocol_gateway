#include "lattice/executor.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace lattice {
namespace {

[[nodiscard]] Error executor_error(ErrorCode code, std::string detail) {
  return make_error(ErrorScope::internal, code, CloseAction::none, std::move(detail));
}

}  // namespace

DeterministicExecutor::DeterministicExecutor(std::size_t capacity, std::uint32_t shards)
    : capacity_(capacity), shards_(shards == 0U ? 1U : shards) {}

Result<std::uint64_t> DeterministicExecutor::submit(std::uint32_t shard,
                                                    std::function<Result<void>()> task) {
  if (!task) {
    return executor_error(ErrorCode::illegal_state, "executor task is empty");
  }
  if (shard >= shards_) {
    return executor_error(ErrorCode::resource_limit, "executor shard is out of range");
  }
  if (queue_.size() >= capacity_) {
    return executor_error(ErrorCode::would_block, "executor queue is full");
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
    return executor_error(ErrorCode::illegal_state, "executor task is not queued");
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

struct ThreadedExecutor::Impl {
  struct Shard {
    std::deque<ExecutorTask> queue;
    std::thread worker;
  };

  explicit Impl(std::size_t max_tasks, std::uint32_t shards)
      : capacity(max_tasks), shard_count(shards == 0U ? 1U : shards), queues(shard_count) {}

  std::size_t capacity{0};
  std::uint32_t shard_count{1};
  std::uint64_t next_id{1};
  std::vector<Shard> queues;
  mutable std::mutex mutex;
  std::condition_variable ready;
  std::condition_variable idle;
  bool stopping{false};
  std::size_t queued{0};
  std::size_t active{0};
  std::optional<Error> first_error;
};

ThreadedExecutor::ThreadedExecutor(std::size_t capacity, std::uint32_t shards)
    : impl_(std::make_unique<Impl>(capacity, shards)), shard_count_(impl_->shard_count) {
  for (std::uint32_t shard = 0; shard < impl_->shard_count; ++shard) {
    impl_->queues[shard].worker = std::thread([this, shard] {
      for (;;) {
        ExecutorTask task;
        {
          std::unique_lock<std::mutex> lock(impl_->mutex);
          impl_->ready.wait(lock, [&] {
            return impl_->stopping || !impl_->queues[shard].queue.empty();
          });
          if (impl_->queues[shard].queue.empty()) {
            if (impl_->stopping) {
              return;
            }
            continue;
          }
          task = std::move(impl_->queues[shard].queue.front());
          impl_->queues[shard].queue.pop_front();
          --impl_->queued;
          ++impl_->active;
        }

        auto result = task.run();

        {
          std::lock_guard<std::mutex> lock(impl_->mutex);
          if (!result && !impl_->first_error.has_value()) {
            impl_->first_error = result.error();
          }
          --impl_->active;
          if (impl_->queued == 0U && impl_->active == 0U) {
            impl_->idle.notify_all();
          }
        }
      }
    });
  }
}

ThreadedExecutor::~ThreadedExecutor() {
  (void)shutdown();
}

Result<std::uint64_t> ThreadedExecutor::submit(std::uint32_t shard,
                                               std::function<Result<void>()> task) {
  if (!task) {
    return executor_error(ErrorCode::illegal_state, "executor task is empty");
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->stopping) {
    return executor_error(ErrorCode::illegal_state, "executor is shutting down");
  }
  if (shard >= impl_->shard_count) {
    return executor_error(ErrorCode::resource_limit, "executor shard is out of range");
  }
  if (impl_->queued >= impl_->capacity) {
    return executor_error(ErrorCode::would_block, "executor queue is full");
  }
  const std::uint64_t id = impl_->next_id++;
  impl_->queues[shard].queue.push_back(ExecutorTask{id, shard, std::move(task)});
  ++impl_->queued;
  impl_->ready.notify_all();
  return id;
}

Result<void> ThreadedExecutor::wait_idle() {
  std::unique_lock<std::mutex> lock(impl_->mutex);
  impl_->idle.wait(lock, [&] { return impl_->queued == 0U && impl_->active == 0U; });
  if (impl_->first_error.has_value()) {
    return impl_->first_error.value();
  }
  return {};
}

Result<void> ThreadedExecutor::shutdown() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping) {
      return {};
    }
    impl_->stopping = true;
  }
  impl_->ready.notify_all();
  for (Impl::Shard& shard : impl_->queues) {
    if (shard.worker.joinable()) {
      shard.worker.join();
    }
  }
  return {};
}

std::size_t ThreadedExecutor::queued() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->queued;
}

}  // namespace lattice
