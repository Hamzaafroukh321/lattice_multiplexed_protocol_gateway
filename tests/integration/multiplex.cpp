#include "test_support.hpp"

using namespace lattice;

namespace {

void handshake(ConnectionEngine& left, ConnectionEngine& right) {
  auto left_hello = left.start();
  auto right_hello = right.start();
  REQUIRE_OK(left_hello);
  REQUIRE_OK(right_hello);
  REQUIRE_OK(right.receive(left_hello.value()[0], false));
  REQUIRE_OK(left.receive(right_hello.value()[0], false));
  CHECK(left.state() == ConnectionState::active);
  CHECK(right.state() == ConnectionState::active);
}

}  // namespace

static void TwoMemoryTransportsCompleteHello() {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  handshake(left, right);
  CHECK(left.capabilities()->plugins.size() == 1U);
  CHECK(right.capabilities()->plugins.size() == 1U);
}

static void TwoFragmentMessageDeliveryThroughEchoPlugin() {
  LocalPolicy policy;
  policy.max_frame = 128U;
  ConnectionEngine left(policy, make_registry());
  ConnectionEngine right(policy, make_registry());
  handshake(left, right);
  auto opened = left.open_channel(OpenRequest{7U, 1024U});
  REQUIRE_OK(opened);
  REQUIRE_OK(right.receive(opened.value().second[0], false));
  Bytes payload(100U, 'x');
  auto sent = left.send(opened.value().first, payload);
  REQUIRE_OK(sent);
  CHECK(sent.value().size() > 1U);
  for (const Bytes& frame : sent.value()) {
    REQUIRE_OK(right.receive(frame, false));
  }
  bool delivered = false;
  bool echoed = false;
  for (const auto& event : right.events()) {
    if (event.kind == ConnectionEvent::Kind::message_delivered) {
      delivered = event.payload == payload;
    }
    if (event.kind == ConnectionEvent::Kind::plugin_response) {
      echoed = event.payload.size() == payload.size() + 5U;
    }
  }
  CHECK(delivered);
  CHECK(echoed);
}

static void LateFrameForOldGenerationRejected() {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  handshake(left, right);
  auto opened = left.open_channel(OpenRequest{7U, 1024U});
  REQUIRE_OK(opened);
  REQUIRE_OK(right.receive(opened.value().second[0], false));
  REQUIRE_OK(right.reset(opened.value().first));
  auto stale = left.send(opened.value().first, Bytes{'x'});
  REQUIRE_OK(stale);
  auto rejected = right.receive(stale.value()[0], false);
  CHECK(!rejected);
  CHECK(rejected.error().code == ErrorCode::stale_generation);
}

void register_multiplex_tests() {
  add_test("TwoMemoryTransportsCompleteHello", &TwoMemoryTransportsCompleteHello);
  add_test("TwoFragmentMessageDeliveryThroughEchoPlugin", &TwoFragmentMessageDeliveryThroughEchoPlugin);
  add_test("LateFrameForOldGenerationRejected", &LateFrameForOldGenerationRejected);
}
