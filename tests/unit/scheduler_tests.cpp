#include "test_support.hpp"

#include "lattice/scheduler.hpp"

using namespace lattice;

static OutboundItem item(OutboundPriority priority, std::uint32_t channel, std::uint32_t sequence,
                         std::uint8_t byte) {
  return OutboundItem{priority, ChannelId{channel, 1U}, sequence, Bytes{byte}};
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

void register_scheduler_tests() {
  add_test("SchedulerBoundsQueues", &SchedulerBoundsQueues);
  add_test("ControlDrainsBeforeData", &ControlDrainsBeforeData);
  add_test("PerChannelOrderUnderPriority", &PerChannelOrderUnderPriority);
}
