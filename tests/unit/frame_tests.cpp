#include "test_support.hpp"

#include "lattice/frame.hpp"

using namespace lattice;

static void FrameSplitEveryByte() {
  FrameCodec codec;
  Frame frame;
  frame.type = FrameType::data;
  frame.channel = ChannelId{5U, 1U};
  frame.frame_seq = 42U;
  frame.extensions = {
      Extension{1U, true, Bytes{0U, 0U, 0U, 1U}},
      Extension{2U, true, Bytes{0U, 0U, 0U, 0U}},
      Extension{3U, true, Bytes{0U, 0U, 0U, 5U}},
      Extension{4U, true, Bytes{0U, 0U, 0U, 7U}}};
  frame.payload = Bytes{'h', 'e', 'l', 'l', 'o'};
  auto encoded = codec.encode(frame);
  REQUIRE_OK(encoded);

  FrameCodec streaming;
  std::optional<Frame> decoded;
  for (std::uint8_t byte : encoded.value()) {
    Bytes one{byte};
    for (auto& event : streaming.feed(one, false)) {
      if (event.status == DecodeStatus::frame) {
        decoded = event.frame;
      }
      CHECK(event.status != DecodeStatus::error);
    }
  }
  CHECK(decoded.has_value());
  CHECK(decoded->channel == frame.channel);
  CHECK(decoded->payload == frame.payload);
  CHECK(decoded->extensions.size() == 4U);
}

static void CrcMismatchClosesConnection() {
  FrameCodec codec;
  Frame frame;
  frame.type = FrameType::hello;
  frame.frame_seq = 1U;
  frame.payload = Bytes{'x'};
  auto encoded = codec.encode(frame);
  REQUIRE_OK(encoded);
  encoded.value().back() ^= 0x55U;
  FrameCodec decoder;
  auto events = decoder.feed(encoded.value(), false);
  CHECK(!events.empty());
  CHECK(events.back().status == DecodeStatus::error);
  CHECK(events.back().error->code == ErrorCode::crc_mismatch);
}

static void HelloCanonicalOrder() {
  FrameCodec codec;
  Frame frame;
  frame.type = FrameType::hello;
  frame.frame_seq = 1U;
  frame.extensions = {Extension{9U, false, Bytes{'b'}}, Extension{2U, false, Bytes{'a'}}};
  auto encoded = codec.encode(frame);
  REQUIRE_OK(encoded);
  auto events = codec.feed(encoded.value(), false);
  CHECK(events.size() == 1U);
  CHECK(events[0].status == DecodeStatus::frame);
  CHECK(events[0].frame->extensions[0].type == 2U);
  CHECK(events[0].frame->extensions[1].type == 9U);
}

static void UnknownOptionalExtensionIsSkipped() {
  FrameCodec codec;
  Frame frame;
  frame.type = FrameType::ping;
  frame.frame_seq = 1U;
  frame.payload = encode_u64_be(9U);
  frame.extensions = {Extension{99U, false, Bytes{'o'}}};
  auto encoded = codec.encode(frame);
  REQUIRE_OK(encoded);
  auto events = codec.feed(encoded.value(), false);
  CHECK(events.size() == 1U);
  CHECK(events[0].status == DecodeStatus::frame);
}

static void UnknownRequiredExtensionRejects() {
  FrameCodec codec;
  Frame frame;
  frame.type = FrameType::ping;
  frame.frame_seq = 1U;
  frame.payload = encode_u64_be(9U);
  frame.extensions = {Extension{99U, true, Bytes{'r'}}};
  auto encoded = codec.encode(frame);
  REQUIRE_OK(encoded);
  auto events = codec.feed(encoded.value(), false);
  CHECK(events.size() == 1U);
  CHECK(events[0].status == DecodeStatus::error);
  CHECK(events[0].error->code == ErrorCode::unknown_required_feature);
}

void register_frame_tests() {
  add_test("FrameSplitEveryByte", &FrameSplitEveryByte);
  add_test("CrcMismatchClosesConnection", &CrcMismatchClosesConnection);
  add_test("HelloCanonicalOrder", &HelloCanonicalOrder);
  add_test("UnknownOptionalExtensionIsSkipped", &UnknownOptionalExtensionIsSkipped);
  add_test("UnknownRequiredExtensionRejects", &UnknownRequiredExtensionRejects);
}
