#include "test_support.hpp"

#include "lattice/replay.hpp"

using namespace lattice;

static void AckRangesRetireExactly() {
  ReplayWindow replay(8U);
  REQUIRE_OK(replay.record(1U, 1U, Bytes{'a'}));
  REQUIRE_OK(replay.record(1U, 2U, Bytes{'b'}));
  REQUIRE_OK(replay.record(1U, 3U, Bytes{'c'}));
  auto retired = replay.acknowledge(1U, {AckRange{2U, 2U}});
  REQUIRE_OK(retired);
  CHECK(retired.value().size() == 1U);
  CHECK(retired.value()[0].frame_seq == 2U);
  CHECK(replay.contains(1U));
  CHECK(!replay.contains(2U));
  CHECK(replay.contains(3U));
}

static void RetransmitBytesIdentical() {
  ReplayWindow replay(8U);
  Bytes bytes{'f', 'r', 'a', 'm', 'e'};
  REQUIRE_OK(replay.record(1U, 9U, bytes));
  auto due = replay.due_for_retry(2U);
  REQUIRE_OK(due);
  CHECK(due.value().size() == 1U);
  CHECK(due.value()[0].encoded == bytes);
}

static void AckPayloadRoundTripRejectsOverlap() {
  auto encoded = encode_ack_payload(1U, {AckRange{3U, 4U}, AckRange{1U, 2U}});
  REQUIRE_OK(encoded);
  auto decoded = decode_ack_payload(encoded.value());
  REQUIRE_OK(decoded);
  CHECK(decoded.value().first == 1U);
  CHECK(decoded.value().second[0].first == 1U);
  CHECK(decoded.value().second[1].last == 4U);

  auto rejected = encode_ack_payload(1U, {AckRange{1U, 3U}, AckRange{3U, 4U}});
  CHECK(!rejected);
  CHECK(rejected.error().code == ErrorCode::sequence_error);
}

static void ResumeWindowTooOldRejects() {
  ReplayWindow replay(2U);
  REQUIRE_OK(replay.record(1U, 10U, Bytes{'a'}));
  REQUIRE_OK(replay.record(1U, 11U, Bytes{'b'}));
  REQUIRE_OK(replay.record(1U, 12U, Bytes{'c'}));
  ResumeProof proof;
  proof.epoch = 1U;
  proof.first_required_seq = 10U;
  auto rejected = replay.can_resume(proof);
  CHECK(!rejected);
  CHECK(rejected.error().code == ErrorCode::resume_rejected);
}

static void RetainedFromReturnsExactSuffix() {
  ReplayWindow replay(4U);
  REQUIRE_OK(replay.record(1U, 10U, Bytes{'a'}));
  REQUIRE_OK(replay.record(1U, 11U, Bytes{'b'}));
  REQUIRE_OK(replay.record(1U, 12U, Bytes{'c'}));
  auto retained = replay.retained_from(1U, 11U);
  REQUIRE_OK(retained);
  CHECK(retained.value().size() == 2U);
  CHECK(retained.value()[0].encoded == Bytes({'b'}));
  CHECK(retained.value()[1].encoded == Bytes({'c'}));
}

static void StaleTimerIgnoredAfterCancel() {
  TimerWheel wheel;
  auto first = wheel.schedule(TimerKind::retry, ChannelId{5U, 1U}, 20U);
  auto second = wheel.schedule(TimerKind::retry, ChannelId{5U, 2U}, 10U);
  REQUIRE_OK(wheel.cancel(first));
  auto expired = wheel.expire(30U);
  CHECK(expired.size() == 1U);
  CHECK(expired[0].id == second);
  CHECK(expired[0].channel.generation == 2U);
}

void register_replay_tests() {
  add_test("AckRangesRetireExactly", &AckRangesRetireExactly);
  add_test("RetransmitBytesIdentical", &RetransmitBytesIdentical);
  add_test("AckPayloadRoundTripRejectsOverlap", &AckPayloadRoundTripRejectsOverlap);
  add_test("ResumeWindowTooOldRejects", &ResumeWindowTooOldRejects);
  add_test("RetainedFromReturnsExactSuffix", &RetainedFromReturnsExactSuffix);
  add_test("StaleTimerIgnoredAfterCancel", &StaleTimerIgnoredAfterCancel);
}
