#include "test_support.hpp"

#include "lattice/inspection.hpp"

#include <algorithm>

using namespace lattice;

namespace {

Bytes u32_bytes(std::uint32_t value) {
  return Bytes{
      static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
      static_cast<std::uint8_t>(value & 0xFFU),
  };
}

Frame make_data_frame(std::uint32_t sequence, std::uint32_t offset,
                      std::uint32_t total, Bytes payload) {
  Frame frame;
  frame.type = FrameType::data;
  frame.channel = ChannelId{1U, 1U};
  frame.frame_seq = sequence;
  frame.payload = std::move(payload);
  frame.extensions.push_back(Extension{1U, true, u32_bytes(sequence)});
  frame.extensions.push_back(Extension{2U, true, u32_bytes(offset)});
  frame.extensions.push_back(Extension{3U, true, u32_bytes(total)});
  frame.extensions.push_back(Extension{4U, true, u32_bytes(7U)});
  return frame;
}

}  // namespace

static void InspectionAcceptsCanonicalDataFrame() {
  FrameCodec codec;
  auto encoded = codec.encode(make_data_frame(1U, 0U, 2U, Bytes{'o', 'k'}));
  REQUIRE_OK(encoded);

  auto report = inspect_frame_stream(encoded.value());
  CHECK(report.ok());
  CHECK(report.frames.size() == 1U);
  CHECK(report.data_frames == 1U);
  CHECK(report.control_frames == 0U);
  CHECK(report.payload_bytes == 2U);
  CHECK(report.frames[0].required_extension_count == 4U);
  CHECK(format_inspection_report(report).find("type=DATA") != std::string::npos);
}

static void InspectionReportsMissingDataExtensions() {
  Frame frame;
  frame.type = FrameType::data;
  frame.channel = ChannelId{3U, 1U};
  frame.frame_seq = 7U;
  frame.payload = Bytes{'x'};

  auto findings = inspect_frame(frame, 0U);
  CHECK(findings.size() == 4U);
  CHECK(std::all_of(findings.begin(), findings.end(), [](const FrameFinding& finding) {
    return finding.severity == InspectionSeverity::error;
  }));
}

static void InspectionReportsDecoderErrors() {
  const Bytes bad{'n', 'o', 't', '-', 'l', 't', 'x'};
  auto report = inspect_frame_stream(bad);
  CHECK(!report.ok());
  CHECK(report.error_count() == 1U);
  CHECK(report.findings[0].code == FrameFindingCode::decoder_error);
}

static void InspectionWarnsOnSequenceRewind() {
  FrameCodec codec;
  auto first = codec.encode(make_data_frame(5U, 0U, 1U, Bytes{'a'}));
  auto second = codec.encode(make_data_frame(4U, 0U, 1U, Bytes{'b'}));
  REQUIRE_OK(first);
  REQUIRE_OK(second);
  Bytes stream = first.value();
  stream.insert(stream.end(), second.value().begin(), second.value().end());

  auto report = inspect_frame_stream(stream);
  CHECK(report.warning_count() == 1U);
  CHECK(report.findings[0].code == FrameFindingCode::sequence_rewind);
}

static void InspectionSummarizesConnectionEvents() {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  auto left_hello = left.start();
  auto right_hello = right.start();
  REQUIRE_OK(left_hello);
  REQUIRE_OK(right_hello);
  REQUIRE_OK(right.receive(left_hello.value()[0], false));
  REQUIRE_OK(left.receive(right_hello.value()[0], false));
  auto opened = left.open_channel(OpenRequest{7U, 1024U});
  REQUIRE_OK(opened);
  REQUIRE_OK(right.receive(opened.value().second[0], false));
  auto sent = left.send(opened.value().first, Bytes{'h', 'i'});
  REQUIRE_OK(sent);
  for (const Bytes& frame : sent.value()) {
    REQUIRE_OK(right.receive(frame, false));
  }

  auto summary = summarize_connection_events(right.events());
  CHECK(summary.total >= 2U);
  CHECK(summary.delivered == 1U);
  CHECK(summary.payload_bytes >= 2U);
  CHECK(!summary.channels.empty());
  CHECK(format_connection_event_summary(summary).find("delivered=1") != std::string::npos);
}

void register_inspection_tests() {
  add_test("InspectionAcceptsCanonicalDataFrame", &InspectionAcceptsCanonicalDataFrame);
  add_test("InspectionReportsMissingDataExtensions", &InspectionReportsMissingDataExtensions);
  add_test("InspectionReportsDecoderErrors", &InspectionReportsDecoderErrors);
  add_test("InspectionWarnsOnSequenceRewind", &InspectionWarnsOnSequenceRewind);
  add_test("InspectionSummarizesConnectionEvents", &InspectionSummarizesConnectionEvents);
}
