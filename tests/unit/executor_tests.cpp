#include "test_support.hpp"

#include "lattice/executor.hpp"

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

void register_executor_tests() {
  add_test("ExecutorBoundsAdmission", &ExecutorBoundsAdmission);
  add_test("ExecutorDrainsDeterministically", &ExecutorDrainsDeterministically);
  add_test("ExecutorCancelPreventsLateRun", &ExecutorCancelPreventsLateRun);
  add_test("ConnectionShardRouterAssignsStableShards",
           &ConnectionShardRouterAssignsStableShards);
}
