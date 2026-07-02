#include "test_support.hpp"

#include "lattice/executor.hpp"

#include <mutex>

using namespace lattice;

static void ExecutorBoundsAdmission() {
  DeterministicExecutor executor(1U, 2U);
  REQUIRE_OK(executor.submit(0U, [] { return Result<void>{}; }));
  auto blocked = executor.submit(1U, [] { return Result<void>{}; });
  CHECK(!blocked);
  CHECK(blocked.error().code == ErrorCode::would_block);
}

static void ExecutorDrainsDeterministically() {
  DeterministicExecutor executor(4U, 2U);
  std::vector<std::uint32_t> order;
  REQUIRE_OK(executor.submit(1U, [&] {
    order.push_back(1U);
    return Result<void>{};
  }));
  REQUIRE_OK(executor.submit(0U, [&] {
    order.push_back(0U);
    return Result<void>{};
  }));
  REQUIRE_OK(executor.drain_all());
  CHECK(order == std::vector<std::uint32_t>({1U, 0U}));
}

static void ExecutorCancelPreventsLateRun() {
  DeterministicExecutor executor(4U, 1U);
  bool ran = false;
  auto id = executor.submit(0U, [&] {
    ran = true;
    return Result<void>{};
  });
  REQUIRE_OK(id);
  REQUIRE_OK(executor.cancel(id.value()));
  REQUIRE_OK(executor.drain_all());
  CHECK(!ran);
}

static void ConnectionShardRouterAssignsStableShards() {
  DeterministicExecutor executor(4U, 3U);
  ConnectionShardRouter router(executor);
  CHECK(router.shard_for(7U) == router.shard_for(7U));
  CHECK(router.shard_for(7U) == 1U);
  int ran = 0;
  REQUIRE_OK(router.submit(7U, [&] {
    ran = 7;
    return Result<void>{};
  }));
  REQUIRE_OK(executor.drain_all());
  CHECK(ran == 7);
}

static void ThreadedExecutorRunsShardTasksInOrder() {
  ThreadedExecutor executor(8U, 2U);
  std::mutex mutex;
  std::vector<int> shard_zero;
  std::vector<int> all;
  REQUIRE_OK(executor.submit(0U, [&] {
    std::lock_guard<std::mutex> lock(mutex);
    shard_zero.push_back(1);
    all.push_back(1);
    return Result<void>{};
  }));
  REQUIRE_OK(executor.submit(0U, [&] {
    std::lock_guard<std::mutex> lock(mutex);
    shard_zero.push_back(2);
    all.push_back(2);
    return Result<void>{};
  }));
  REQUIRE_OK(executor.submit(1U, [&] {
    std::lock_guard<std::mutex> lock(mutex);
    all.push_back(10);
    return Result<void>{};
  }));
  REQUIRE_OK(executor.wait_idle());
  CHECK(shard_zero == std::vector<int>({1, 2}));
  CHECK(all.size() == 3U);
}

static void ThreadedExecutorReportsTaskError() {
  ThreadedExecutor executor(2U, 1U);
  REQUIRE_OK(executor.submit(0U, [] {
    return make_error(ErrorScope::internal, ErrorCode::invariant_failure,
                      CloseAction::none, "threaded executor test error");
  }));
  auto idle = executor.wait_idle();
  CHECK(!idle);
  CHECK(idle.error().code == ErrorCode::invariant_failure);
}

static void ThreadedExecutorRejectsAfterShutdown() {
  ThreadedExecutor executor(1U, 1U);
  REQUIRE_OK(executor.shutdown());
  auto rejected = executor.submit(0U, [] { return Result<void>{}; });
  CHECK(!rejected);
  CHECK(rejected.error().code == ErrorCode::illegal_state);
}

void register_executor_tests() {
  add_test("ExecutorBoundsAdmission", &ExecutorBoundsAdmission);
  add_test("ExecutorDrainsDeterministically", &ExecutorDrainsDeterministically);
  add_test("ExecutorCancelPreventsLateRun", &ExecutorCancelPreventsLateRun);
  add_test("ConnectionShardRouterAssignsStableShards",
           &ConnectionShardRouterAssignsStableShards);
  add_test("ThreadedExecutorRunsShardTasksInOrder", &ThreadedExecutorRunsShardTasksInOrder);
  add_test("ThreadedExecutorReportsTaskError", &ThreadedExecutorReportsTaskError);
  add_test("ThreadedExecutorRejectsAfterShutdown", &ThreadedExecutorRejectsAfterShutdown);
}
