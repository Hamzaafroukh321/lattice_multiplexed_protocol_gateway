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

static void HandshakeTimeoutClosesConnection() {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  auto hello = left.start();
  REQUIRE_OK(hello);
  auto expired = left.advance_time(5000U);
  CHECK(!expired);
  CHECK(expired.error().code == ErrorCode::timeout);
  CHECK(left.state() == ConnectionState::closed);
}

static void PingProducesPongAndEvent() {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  handshake(left, right);
  auto ping = left.ping(0x0102030405060708ULL);
  REQUIRE_OK(ping);
  CHECK(ping.value().size() == 1U);
  auto pong = right.receive(ping.value()[0], false);
  REQUIRE_OK(pong);
  CHECK(pong.value().size() == 1U);
  REQUIRE_OK(left.receive(pong.value()[0], false));
  bool saw_pong = false;
  for (const auto& event : left.events()) {
    if (event.kind == ConnectionEvent::Kind::pong_received) {
      saw_pong = true;
    }
  }
  CHECK(saw_pong);
}

static void ResumeTranscriptMismatchRejects() {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  handshake(left, right);
  ResumeRequest request;
  request.epoch = 1U;
  request.first_required_seq = 1U;
  auto frame = left.resume(request);
  REQUIRE_OK(frame);
  auto rejected = right.receive(frame.value()[0], false);
  CHECK(!rejected);
  CHECK(rejected.error().code == ErrorCode::resume_rejected);
}

static void AsyncResultAfterResetDropped() {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  handshake(left, right);
  auto opened = left.open_channel(OpenRequest{7U, 1024U});
  REQUIRE_OK(opened);
  REQUIRE_OK(right.receive(opened.value().second[0], false));
  REQUIRE_OK(right.reset(opened.value().first));

  PluginCompletion completion;
  completion.token = 99U;
  completion.channel = opened.value().first;
  completion.sequence = 1U;
  completion.family_id = 7U;
  completion.response = Bytes{'l', 'a', 't', 'e'};
  REQUIRE_OK(right.complete_plugin(std::move(completion)));

  for (const auto& event : right.events()) {
    CHECK(!(event.kind == ConnectionEvent::Kind::plugin_response && event.payload == Bytes({'l', 'a', 't', 'e'})));
  }
}

void register_multiplex_tests() {
  add_test("TwoMemoryTransportsCompleteHello", &TwoMemoryTransportsCompleteHello);
  add_test("TwoFragmentMessageDeliveryThroughEchoPlugin", &TwoFragmentMessageDeliveryThroughEchoPlugin);
  add_test("LateFrameForOldGenerationRejected", &LateFrameForOldGenerationRejected);
  add_test("HandshakeTimeoutClosesConnection", &HandshakeTimeoutClosesConnection);
  add_test("PingProducesPongAndEvent", &PingProducesPongAndEvent);
  add_test("ResumeTranscriptMismatchRejects", &ResumeTranscriptMismatchRejects);
  add_test("AsyncResultAfterResetDropped", &AsyncResultAfterResetDropped);
}
