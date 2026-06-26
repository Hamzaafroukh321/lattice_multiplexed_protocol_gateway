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

void register_replay_tests() {
  add_test("AckRangesRetireExactly", &AckRangesRetireExactly);
  add_test("RetransmitBytesIdentical", &RetransmitBytesIdentical);
}
