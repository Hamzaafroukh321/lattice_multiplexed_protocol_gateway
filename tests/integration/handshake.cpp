#include "test_support.hpp"

using namespace lattice;

static void LimitIntersectionUsesMinimum() {
  auto left_registry = make_registry();
  auto right_registry = make_registry();
  LocalPolicy left_policy;
  LocalPolicy right_policy;
  left_policy.max_frame = 65536U;
  right_policy.max_frame = 4096U;
  ConnectionEngine left(left_policy, std::move(left_registry));
  ConnectionEngine right(right_policy, std::move(right_registry));
  auto left_hello = left.start();
  auto right_hello = right.start();
  REQUIRE_OK(left_hello);
  REQUIRE_OK(right_hello);
  REQUIRE_OK(right.receive(left_hello.value()[0], false));
  REQUIRE_OK(left.receive(right_hello.value()[0], false));
  CHECK(left.state() == ConnectionState::active);
  CHECK(right.state() == ConnectionState::active);
  CHECK(left.capabilities()->max_frame == 4096U);
  CHECK(right.capabilities()->max_frame == 4096U);
  CHECK(left.capabilities()->transcript_hash == right.capabilities()->transcript_hash);
}

static void SchemaHashMismatchRejectsOpen() {
  PluginRegistry left_registry = make_registry();
  PluginRegistry right_registry;
  REQUIRE_OK(right_registry.register_factory(PluginDescriptor{7U, 0x1234U, 4U}, [] {
    return std::make_unique<EchoPlugin>();
  }));
  ConnectionEngine left(LocalPolicy{}, std::move(left_registry));
  ConnectionEngine right(LocalPolicy{}, std::move(right_registry));
  auto left_hello = left.start();
  auto right_hello = right.start();
  REQUIRE_OK(left_hello);
  REQUIRE_OK(right_hello);
  auto rejected = right.receive(left_hello.value()[0], false);
  CHECK(!rejected);
  CHECK(rejected.error().code == ErrorCode::negotiation_rejected);
}

void register_handshake_tests() {
  add_test("LimitIntersectionUsesMinimum", &LimitIntersectionUsesMinimum);
  add_test("SchemaHashMismatchRejectsOpen", &SchemaHashMismatchRejectsOpen);
}
