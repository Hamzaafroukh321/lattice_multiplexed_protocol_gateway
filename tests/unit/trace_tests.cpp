#include "test_support.hpp"

#include "lattice/trace.hpp"

using namespace lattice;

static void TraceRoundTripIsDeterministic() {
  TraceLog log;
  log.record(TraceEvent{20U, TraceKind::timer, "retry", Bytes{0x02U}});
  log.record(TraceEvent{10U, TraceKind::transport_bytes, "in", Bytes{0x4cU, 0x54U}});
  auto serialized = log.serialize();
  REQUIRE_OK(serialized);
  auto parsed = TraceLog::parse(serialized.value());
  REQUIRE_OK(parsed);
  CHECK(parsed.value().events().size() == 2U);
  CHECK(parsed.value().events()[0].time_ms == 10U);
  CHECK(parsed.value().events()[0].bytes == Bytes({0x4cU, 0x54U}));
}

static void TraceRejectsMalformedHex() {
  auto parsed = TraceLog::parse("LTXTRACE/1\n1|api|event|abc\n");
  CHECK(!parsed);
  CHECK(parsed.error().code == ErrorCode::resource_limit);
}

void register_trace_tests() {
  add_test("TraceRoundTripIsDeterministic", &TraceRoundTripIsDeterministic);
  add_test("TraceRejectsMalformedHex", &TraceRejectsMalformedHex);
}
