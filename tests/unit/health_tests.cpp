#include "test_support.hpp"

#include "lattice/health.hpp"

using namespace lattice;

namespace {

Bytes u32_field(std::uint32_t value) {
  return Bytes{
      static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
      static_cast<std::uint8_t>(value & 0xFFU),
  };
}

Frame health_data_frame(std::uint32_t sequence, Bytes payload) {
  Frame frame;
  frame.type = FrameType::data;
  frame.channel = ChannelId{11U, 1U};
  frame.frame_seq = sequence;
  frame.payload = std::move(payload);
  frame.extensions.push_back(Extension{1U, true, u32_field(sequence)});
  frame.extensions.push_back(Extension{2U, true, u32_field(0U)});
  frame.extensions.push_back(Extension{3U, true, u32_field(1U)});
  frame.extensions.push_back(Extension{4U, true, u32_field(7U)});
  return frame;
}

StreamInspectionReport inspected_single_data_frame() {
  FrameCodec codec;
  auto encoded = codec.encode(health_data_frame(1U, Bytes{'o', 'k'}));
  REQUIRE_OK(encoded);
  return inspect_frame_stream(encoded.value());
}

}  // namespace

static void HealthEvaluatesCleanStream() {
  auto inspection = inspected_single_data_frame();
  auto health = evaluate_stream_health(inspection);
  CHECK(health.level == HealthLevel::healthy);
  CHECK(health.score >= 90U);
  CHECK(health.failed_checks == 0U);
  CHECK(format_health_report(health).find("health=healthy") != std::string::npos);
}

static void HealthFailsDecoderRejectedStream() {
  const Bytes bad{'b', 'a', 'd'};
  auto inspection = inspect_frame_stream(bad);
  auto health = evaluate_stream_health(inspection);
  CHECK(health.level == HealthLevel::failed);
  CHECK(!health.ok());
  CHECK(!health.signals.empty());
  CHECK(health.signals[0].code == HealthSignalCode::stream_empty ||
        health.signals[0].code == HealthSignalCode::stream_decoder_errors);
}

static void HealthWarnsOnControlOnlyStream() {
  Frame frame;
  frame.type = FrameType::ping;
  frame.channel = ChannelId{};
  frame.frame_seq = 1U;
  frame.payload = encode_u64_be(7U);
  FrameCodec codec;
  auto encoded = codec.encode(frame);
  REQUIRE_OK(encoded);
  auto inspection = inspect_frame_stream(encoded.value());
  auto health = evaluate_stream_health(inspection);
  CHECK(health.level == HealthLevel::degraded);
  CHECK(health.degraded_checks >= 1U);
}

static void HealthEvaluatesConnectionSummary() {
  ConnectionEventSummary summary;
  summary.total = 4U;
  summary.negotiated = 1U;
  summary.opened = 1U;
  summary.delivered = 1U;
  summary.payload_bytes = 4U;
  summary.channels.push_back(ChannelId{1U, 1U});
  auto health = evaluate_connection_health(summary);
  CHECK(health.level == HealthLevel::healthy);
  CHECK(health.ok());
}

static void HealthFailsUnnegotiatedConnection() {
  ConnectionEventSummary summary;
  summary.total = 2U;
  summary.diagnostics = 1U;
  summary.payload_bytes = 5U;
  auto health = evaluate_connection_health(summary);
  CHECK(health.level == HealthLevel::failed);
  CHECK(health.failed_checks >= 1U);
}

static void HealthEvaluatesRoutePolicy() {
  const std::string text =
      "[route]\n"
      "name=\"primary\"\n"
      "source=1:1\n"
      "destination=2:1\n"
      "family=7\n"
      "payload_hex=6f6b\n";
  auto parsed = parse_route_policy_text(text);
  REQUIRE_OK(parsed);
  auto health = evaluate_route_policy_health(parsed.value());
  CHECK(health.level == HealthLevel::healthy);
  CHECK(health.score >= 90U);
}

static void HealthDetectsSparseRoutePolicy() {
  RoutePolicyDocument document;
  document.routes.push_back(RoutePolicyEntry{"one", ChannelId{1U, 1U},
                                             ChannelId{2U, 1U}, 7U, Bytes{}});
  HealthOptions options;
  options.min_routes = 2U;
  auto health = evaluate_route_policy_health(document, options);
  CHECK(health.level == HealthLevel::degraded);
  CHECK(health.degraded_checks >= 1U);
}

static void HealthFailsRouteLoopback() {
  RoutePolicyDocument document;
  document.routes.push_back(RoutePolicyEntry{"loop", ChannelId{9U, 1U},
                                             ChannelId{9U, 1U}, 7U, Bytes{'x'}});
  auto health = evaluate_route_policy_health(document);
  CHECK(health.level == HealthLevel::failed);
  CHECK(health.failed_checks >= 1U);
}

static void HealthMergesReports() {
  auto stream = evaluate_stream_health(inspected_single_data_frame());
  ConnectionEventSummary summary;
  summary.total = 1U;
  summary.diagnostics = 1U;
  auto connection = evaluate_connection_health(summary);
  auto merged = merge_health_reports(std::vector<HealthReport>{stream, connection});
  CHECK(merged.level == HealthLevel::failed);
  CHECK(merged.score < 90U);
  CHECK(format_health_report(merged).find("aggregate") != std::string::npos);
}

static void HealthFiltersSignalsByLevel() {
  auto inspection = inspect_frame_stream(Bytes{'b', 'a', 'd'});
  auto report = evaluate_stream_health(inspection);
  auto failed = filter_health_signals(report, HealthLevel::failed);
  auto degraded = filter_health_signals(report, HealthLevel::degraded);
  CHECK(!failed.empty());
  CHECK(degraded.size() >= failed.size());
  CHECK(health_level_at_least(HealthLevel::failed, HealthLevel::degraded));
  CHECK(!health_level_at_least(HealthLevel::healthy, HealthLevel::degraded));
}

static void HealthSummarizesSignalsByCode() {
  auto inspection = inspect_frame_stream(Bytes{'b', 'a', 'd'});
  auto report = evaluate_stream_health(inspection);
  auto buckets = summarize_health_signals(report);
  CHECK(!buckets.empty());
  CHECK(buckets[0].strongest_level == HealthLevel::failed);
  CHECK(format_health_signal_summary(buckets).find("signal_buckets=") !=
        std::string::npos);
}

void register_health_tests() {
  add_test("HealthEvaluatesCleanStream", &HealthEvaluatesCleanStream);
  add_test("HealthFailsDecoderRejectedStream", &HealthFailsDecoderRejectedStream);
  add_test("HealthWarnsOnControlOnlyStream", &HealthWarnsOnControlOnlyStream);
  add_test("HealthEvaluatesConnectionSummary", &HealthEvaluatesConnectionSummary);
  add_test("HealthFailsUnnegotiatedConnection", &HealthFailsUnnegotiatedConnection);
  add_test("HealthEvaluatesRoutePolicy", &HealthEvaluatesRoutePolicy);
  add_test("HealthDetectsSparseRoutePolicy", &HealthDetectsSparseRoutePolicy);
  add_test("HealthFailsRouteLoopback", &HealthFailsRouteLoopback);
  add_test("HealthMergesReports", &HealthMergesReports);
  add_test("HealthFiltersSignalsByLevel", &HealthFiltersSignalsByLevel);
  add_test("HealthSummarizesSignalsByCode", &HealthSummarizesSignalsByCode);
}
