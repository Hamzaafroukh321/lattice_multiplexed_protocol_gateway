#include "test_support.hpp"

#include "lattice/scheduler.hpp"

using namespace lattice;

static OutboundItem item(OutboundPriority priority, std::uint32_t channel, std::uint32_t sequence,
                         std::uint8_t byte) {
  return OutboundItem{priority, ChannelId{channel, 1U}, sequence, Bytes{byte}};
}

static OutboundItem bytes_item(OutboundPriority priority, std::uint32_t channel,
                               std::uint32_t sequence, Bytes bytes) {
  return OutboundItem{priority, ChannelId{channel, 1U}, sequence, std::move(bytes)};
}

static void SchedulerBoundsQueues() {
  OutboundScheduler scheduler(2U);
  REQUIRE_OK(scheduler.enqueue(item(OutboundPriority::data, 1U, 1U, 'a')));
  REQUIRE_OK(scheduler.enqueue(item(OutboundPriority::data, 1U, 2U, 'b')));
  auto blocked = scheduler.enqueue(item(OutboundPriority::data, 1U, 3U, 'c'));
  CHECK(!blocked);
  CHECK(blocked.error().code == ErrorCode::would_block);
}

static void ControlDrainsBeforeData() {
  OutboundScheduler scheduler(8U);
  REQUIRE_OK(scheduler.enqueue(item(OutboundPriority::data, 1U, 1U, 'd')));
  REQUIRE_OK(scheduler.enqueue(item(OutboundPriority::control, 0U, 0U, 'c')));
  auto batch = scheduler.drain(8U);
  CHECK(batch.size() == 2U);
  CHECK(batch[0].priority == OutboundPriority::control);
  CHECK(batch[1].priority == OutboundPriority::data);
}

static void PerChannelOrderUnderPriority() {
  OutboundScheduler scheduler(8U);
  REQUIRE_OK(scheduler.enqueue(item(OutboundPriority::data, 2U, 1U, 'a')));
  REQUIRE_OK(scheduler.enqueue(item(OutboundPriority::data, 2U, 2U, 'b')));
  auto batch = scheduler.drain(8U);
  CHECK(batch.size() == 1U);
  CHECK(batch[0].sequence == 1U);
  batch = scheduler.drain(8U);
  CHECK(batch.size() == 1U);
  CHECK(batch[0].sequence == 2U);
}

static void PartialWriteRetainsLease() {
  OutboundScheduler scheduler(16U);
  REQUIRE_OK(scheduler.enqueue(bytes_item(OutboundPriority::data, 3U, 1U,
                                          Bytes{'a', 'b', 'c', 'd'})));
  REQUIRE_OK(scheduler.enqueue(bytes_item(OutboundPriority::data, 3U, 2U,
                                          Bytes{'e', 'f'})));
  auto batch = scheduler.drain(2U);
  CHECK(batch.size() == 1U);
  CHECK(batch[0].sequence == 1U);
  CHECK(batch[0].encoded == Bytes({'a', 'b'}));
  CHECK(scheduler.queued_bytes() == 4U);

  batch = scheduler.drain(3U);
  CHECK(batch.size() == 1U);
  CHECK(batch[0].sequence == 1U);
  CHECK(batch[0].encoded == Bytes({'c', 'd'}));
  CHECK(scheduler.queued_bytes() == 2U);

  batch = scheduler.drain(2U);
  CHECK(batch.size() == 1U);
  CHECK(batch[0].sequence == 2U);
  CHECK(batch[0].encoded == Bytes({'e', 'f'}));
  CHECK(scheduler.empty());
}

static void PartialControlDrainsBeforeDataTail() {
  OutboundScheduler scheduler(16U);
  REQUIRE_OK(scheduler.enqueue(bytes_item(OutboundPriority::data, 1U, 1U, Bytes{'d'})));
  REQUIRE_OK(scheduler.enqueue(bytes_item(OutboundPriority::control, 0U, 0U,
                                          Bytes{'c', 't'})));
  auto batch = scheduler.drain(1U);
  CHECK(batch.size() == 1U);
  CHECK(batch[0].priority == OutboundPriority::control);
  CHECK(batch[0].encoded == Bytes({'c'}));

  batch = scheduler.drain(2U);
  CHECK(batch.size() == 2U);
  CHECK(batch[0].priority == OutboundPriority::control);
  CHECK(batch[0].encoded == Bytes({'t'}));
  CHECK(batch[1].priority == OutboundPriority::data);
  CHECK(batch[1].encoded == Bytes({'d'}));
}

static void SustainedBackpressurePreservesByteStream() {
  OutboundScheduler scheduler(4096U);
  Bytes expected;
  for (std::uint32_t sequence = 1U; sequence <= 64U; ++sequence) {
    Bytes bytes{
        static_cast<std::uint8_t>('A' + (sequence % 26U)),
        static_cast<std::uint8_t>('a' + (sequence % 26U)),
        static_cast<std::uint8_t>('0' + (sequence % 10U))};
    expected.insert(expected.end(), bytes.begin(), bytes.end());
    REQUIRE_OK(scheduler.enqueue(bytes_item(OutboundPriority::data, 9U, sequence,
                                            std::move(bytes))));
  }

  Bytes actual;
  while (!scheduler.empty()) {
    auto batch = scheduler.drain(1U);
    CHECK(batch.size() == 1U);
    CHECK(batch[0].encoded.size() == 1U);
    actual.push_back(batch[0].encoded[0]);
  }
  CHECK(actual == expected);
}

void register_scheduler_tests() {
  add_test("SchedulerBoundsQueues", &SchedulerBoundsQueues);
  add_test("ControlDrainsBeforeData", &ControlDrainsBeforeData);
  add_test("PerChannelOrderUnderPriority", &PerChannelOrderUnderPriority);
  add_test("PartialWriteRetainsLease", &PartialWriteRetainsLease);
  add_test("PartialControlDrainsBeforeDataTail", &PartialControlDrainsBeforeDataTail);
  add_test("SustainedBackpressurePreservesByteStream",
           &SustainedBackpressurePreservesByteStream);
}
