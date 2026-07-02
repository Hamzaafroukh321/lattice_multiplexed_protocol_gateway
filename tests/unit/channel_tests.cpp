#include "test_support.hpp"

#include "lattice/channel.hpp"

using namespace lattice;

static void ChannelGenerationIncrements() {
  ChannelTable table(1U, 1024U, 4096U);
  auto first = table.allocate();
  REQUIRE_OK(first);
  REQUIRE_OK(table.activate(first.value()));
  REQUIRE_OK(table.reset(first.value()));
  REQUIRE_OK(table.retire_tombstone(first.value()));
  auto second = table.allocate();
  REQUIRE_OK(second);
  CHECK(second.value().number == first.value().number);
  CHECK(second.value().generation == static_cast<std::uint8_t>(first.value().generation + 1U));
  CHECK(table.find(first.value()) == nullptr);
}

static void TwoFragmentMessageDelivery() {
  Reassembler reassembler(64U);
  ChannelId id{3U, 1U};
  Bytes first{'h', 'e'};
  Bytes second{'l', 'l', 'o'};
  auto part = reassembler.insert(id, 1U, 0U, 5U, first);
  REQUIRE_OK(part);
  CHECK(!part.value().has_value());
  auto done = reassembler.insert(id, 1U, 2U, 5U, second);
  REQUIRE_OK(done);
  CHECK(done.value().has_value());
  CHECK(done.value()->payload == Bytes({'h', 'e', 'l', 'l', 'o'}));
  CHECK(reassembler.retained_bytes() == 0U);
}

static void ConflictingOverlapResets() {
  Reassembler reassembler(64U);
  ChannelId id{3U, 1U};
  Bytes first{'a', 'b', 'c'};
  Bytes second{'X', 'Y'};
  auto part = reassembler.insert(id, 1U, 0U, 4U, first);
  REQUIRE_OK(part);
  auto conflict = reassembler.insert(id, 1U, 1U, 4U, second);
  CHECK(!conflict);
  CHECK(conflict.error().code == ErrorCode::fragment_overlap);
}

static void ConflictingOverlapReleasesRetainedBytes() {
  Reassembler reassembler(64U);
  ChannelId id{3U, 1U};
  REQUIRE_OK(reassembler.insert(id, 1U, 0U, 4U, Bytes{'a', 'b', 'c'}));
  auto conflict = reassembler.insert(id, 1U, 1U, 4U, Bytes{'X'});
  CHECK(!conflict);
  CHECK(reassembler.retained_bytes() == 0U);
}

static void SparseFragmentsRespectRetainedBudget() {
  Reassembler reassembler(4U);
  ChannelId id{4U, 1U};
  REQUIRE_OK(reassembler.insert(id, 1U, 0U, 4U, Bytes{'a'}));
  REQUIRE_OK(reassembler.insert(id, 2U, 0U, 4U, Bytes{'b'}));
  REQUIRE_OK(reassembler.insert(id, 3U, 0U, 4U, Bytes{'c'}));
  REQUIRE_OK(reassembler.insert(id, 4U, 0U, 4U, Bytes{'d'}));
  auto rejected = reassembler.insert(id, 5U, 0U, 4U, Bytes{'e'});
  CHECK(!rejected);
  CHECK(rejected.error().code == ErrorCode::resource_limit);
  CHECK(reassembler.retained_bytes() == 4U);
}

static void HalfCloseDirectionsIndependent() {
  ChannelTable table(1U, 1024U, 4096U);
  auto id = table.allocate();
  REQUIRE_OK(id);
  REQUIRE_OK(table.activate(id.value()));
  REQUIRE_OK(table.half_close(id.value(), Direction::local_send));
  const ChannelSlot* slot = table.find(id.value());
  CHECK(slot != nullptr);
  CHECK(slot->state == ChannelState::local_closed);
  REQUIRE_OK(table.half_close(id.value(), Direction::remote_send));
  slot = table.find(id.value());
  CHECK(slot != nullptr);
  CHECK(slot->state == ChannelState::closing);
}

void register_channel_tests() {
  add_test("ChannelGenerationIncrements", &ChannelGenerationIncrements);
  add_test("TwoFragmentMessageDelivery", &TwoFragmentMessageDelivery);
  add_test("ConflictingOverlapResets", &ConflictingOverlapResets);
  add_test("ConflictingOverlapReleasesRetainedBytes",
           &ConflictingOverlapReleasesRetainedBytes);
  add_test("SparseFragmentsRespectRetainedBudget", &SparseFragmentsRespectRetainedBudget);
  add_test("HalfCloseDirectionsIndependent", &HalfCloseDirectionsIndependent);
}
