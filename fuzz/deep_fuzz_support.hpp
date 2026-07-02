#pragma once

#include "lattice/channel.hpp"
#include "lattice/connection.hpp"
#include "lattice/executor.hpp"
#include "lattice/frame.hpp"
#include "lattice/gateway.hpp"
#include "lattice/plugin.hpp"
#include "lattice/replay.hpp"
#include "lattice/scheduler.hpp"
#include "lattice/trace.hpp"
#include "lattice/transport.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lattice::fuzz {

class Input {
 public:
  Input(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  [[nodiscard]] std::size_t remaining() const {
    return pos_ >= size_ ? 0U : size_ - pos_;
  }

  [[nodiscard]] bool empty() const { return remaining() == 0U; }

  std::uint8_t byte(std::uint8_t fallback = 0U) {
    if (pos_ >= size_) {
      return fallback;
    }
    const std::uint8_t value = data_[pos_];
    ++pos_;
    return value;
  }

  bool bit() { return (byte() & 1U) != 0U; }

  std::uint16_t u16(std::uint16_t fallback = 0U) {
    std::uint16_t value = fallback;
    if (remaining() >= 2U) {
      value = static_cast<std::uint16_t>(byte());
      value = static_cast<std::uint16_t>((value << 8U) | byte());
    }
    return value;
  }

  std::uint32_t u32(std::uint32_t fallback = 0U) {
    std::uint32_t value = fallback;
    if (remaining() >= 4U) {
      value = static_cast<std::uint32_t>(byte());
      value = (value << 8U) | byte();
      value = (value << 8U) | byte();
      value = (value << 8U) | byte();
    }
    return value;
  }

  std::uint64_t u64(std::uint64_t fallback = 0U) {
    std::uint64_t value = fallback;
    if (remaining() >= 8U) {
      value = static_cast<std::uint64_t>(u32());
      value = (value << 32U) | u32();
    }
    return value;
  }

  std::size_t bounded(std::size_t max) {
    if (max == 0U) {
      return 0U;
    }
    const std::size_t range = max + 1U;
    return static_cast<std::size_t>(u16()) % range;
  }

  Bytes bytes(std::size_t max) {
    const std::size_t requested = bounded(max);
    const std::size_t count = std::min(requested, remaining());
    Bytes out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      out.push_back(byte());
    }
    return out;
  }

  Bytes fixed_bytes(std::size_t count) {
    Bytes out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      out.push_back(byte(static_cast<std::uint8_t>(i & 0xFFU)));
    }
    return out;
  }

  std::string text(std::size_t max) {
    Bytes raw = bytes(max);
    std::string out;
    out.reserve(raw.size());
    for (std::uint8_t value : raw) {
      out.push_back(static_cast<char>(value));
    }
    return out;
  }

 private:
  const std::uint8_t* data_{nullptr};
  std::size_t size_{0};
  std::size_t pos_{0};
};

using FuzzerEntry = int (*)(const std::uint8_t*, std::size_t);

inline Bytes default_seed() {
  return Bytes{'L', 'T', 'X', '1', '\x01', '\x10', '\x11', '\x12',
               '\x13', '\x14', '\x15', '\x20', '\x21', '\x22', '\x23'};
}

inline int run_standalone(int argc, char** argv, FuzzerEntry entry) {
  Bytes input;
  if (argc > 1 && argv != nullptr && argv[1] != nullptr) {
    std::ifstream in(argv[1], std::ios::binary);
    input.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  if (input.empty()) {
    input = default_seed();
  }
  return entry(input.data(), input.size());
}

inline PluginRegistry make_registry() {
  PluginRegistry registry;
  (void)registry.register_factory(EchoPlugin().descriptor(), [] {
    return std::make_unique<EchoPlugin>();
  });
  return registry;
}

inline void append_u32(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

inline Bytes u32_payload(std::uint32_t value) {
  Bytes out;
  append_u32(out, value);
  return out;
}

inline ChannelId channel_from(Input& input) {
  const std::uint32_t number = 1U + (input.u32(1U) % 32U);
  const std::uint8_t generation = static_cast<std::uint8_t>(1U + (input.byte(1U) % 8U));
  return ChannelId{number, generation};
}

inline FrameType frame_type_from(Input& input) {
  switch (input.byte() % 10U) {
    case 0U:
      return FrameType::hello;
    case 1U:
      return FrameType::open;
    case 2U:
      return FrameType::data;
    case 3U:
      return FrameType::ack;
    case 4U:
      return FrameType::credit;
    case 5U:
      return FrameType::half_close;
    case 6U:
      return FrameType::reset;
    case 7U:
      return FrameType::ping;
    case 8U:
      return FrameType::pong;
    default:
      return FrameType::resume;
  }
}

inline std::vector<Extension> random_extensions(Input& input, std::size_t max_count,
                                                std::size_t max_payload) {
  std::vector<Extension> extensions;
  const std::size_t count = input.bounded(max_count);
  extensions.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint16_t type = static_cast<std::uint16_t>(1U + (input.u16() % 32U));
    const bool required = input.bit();
    extensions.push_back(Extension{type, required, input.bytes(max_payload)});
  }
  return extensions;
}

inline std::vector<Extension> data_extensions(std::uint32_t sequence, std::uint32_t offset,
                                              std::uint32_t total, std::uint32_t family) {
  return std::vector<Extension>{
      Extension{1U, true, u32_payload(sequence)},
      Extension{2U, true, u32_payload(offset)},
      Extension{3U, true, u32_payload(total)},
      Extension{4U, true, u32_payload(family)}};
}

inline Frame random_frame(Input& input, std::size_t max_payload, std::size_t max_ext_payload) {
  Frame frame;
  frame.type = frame_type_from(input);
  frame.channel = frame.type == FrameType::hello || frame.type == FrameType::ack ||
                          frame.type == FrameType::ping || frame.type == FrameType::pong ||
                          frame.type == FrameType::goaway || frame.type == FrameType::resume
                      ? ChannelId{}
                      : channel_from(input);
  frame.frame_seq = input.u32(1U);
  frame.payload = input.bytes(max_payload);
  frame.extensions = random_extensions(input, 6U, max_ext_payload);
  return frame;
}

inline CapabilitySet capability(std::size_t max_message = kDefaultMaxMessage,
                                std::uint64_t schema = EchoPlugin().descriptor().schema_hash) {
  CapabilitySet caps;
  caps.plugins.push_back(PluginDescriptor{7U, schema, 4U});
  caps.max_message = max_message;
  caps.max_frame = kDefaultMaxFrame;
  caps.connection_window = kDefaultConnectionWindow;
  caps.max_channels = kDefaultMaxChannels;
  return caps;
}

inline bool deliver(ConnectionEngine& destination, const std::vector<Bytes>& chunks) {
  for (const Bytes& chunk : chunks) {
    auto received = destination.receive(chunk, false);
    if (!received) {
      return false;
    }
  }
  return true;
}

inline bool handshake(ConnectionEngine& left, ConnectionEngine& right) {
  auto left_hello = left.start();
  auto right_hello = right.start();
  if (!left_hello || !right_hello) {
    return false;
  }
  return deliver(right, left_hello.value()) && deliver(left, right_hello.value()) &&
         left.state() == ConnectionState::active && right.state() == ConnectionState::active;
}

inline std::optional<ChannelId> open_pair(ConnectionEngine& left, ConnectionEngine& right,
                                          std::size_t window = 4096U) {
  auto opened = left.open_channel(OpenRequest{7U, window});
  if (!opened) {
    return std::nullopt;
  }
  if (!deliver(right, opened.value().second)) {
    return std::nullopt;
  }
  return opened.value().first;
}

inline void drain_executor(DeterministicExecutor& executor) {
  for (std::size_t i = 0; i < 64U && executor.queued() > 0U; ++i) {
    auto drained = executor.drain_one();
    if (!drained) {
      return;
    }
  }
}

inline std::uint64_t event_digest(const ConnectionEngine& engine) {
  std::uint64_t digest = 0x9E3779B185EBCA87ULL;
  for (const ConnectionEvent& event : engine.events()) {
    digest ^= static_cast<std::uint64_t>(event.sequence) + 0x9E3779B97F4A7C15ULL +
              (digest << 6U) + (digest >> 2U);
    digest ^= static_cast<std::uint64_t>(event.channel.number) << 17U;
    digest ^= static_cast<std::uint64_t>(event.channel.generation) << 41U;
    digest ^= static_cast<std::uint64_t>(event.payload.size()) << 3U;
  }
  return digest;
}

inline void exercise_decoder(FrameCodec& decoder, std::span<const std::uint8_t> bytes,
                             bool eof) {
  auto events = decoder.feed(bytes, eof);
  for (const DecodeEvent& event : events) {
    if (event.status == DecodeStatus::frame) {
      FrameCodec encoder;
      (void)encoder.encode(event.frame.value());
    }
  }
}

inline void run_frame_stream(Input& input) {
  FrameLimits limits;
  limits.max_frame = 64U + input.bounded(4096U);
  limits.max_header = 16U + input.bounded(512U);
  FrameCodec decoder(limits);
  for (std::size_t i = 0; i < 32U && !input.empty(); ++i) {
    const Bytes chunk = input.bytes(256U);
    exercise_decoder(decoder, chunk, input.bit());
  }
}

inline void run_frame_split(Input& input) {
  FrameCodec encoder;
  Frame frame = random_frame(input, 512U, 32U);
  auto encoded = encoder.encode(frame);
  if (!encoded) {
    return;
  }
  FrameCodec decoder;
  for (std::uint8_t byte : encoded.value()) {
    const std::array<std::uint8_t, 1U> one{byte};
    exercise_decoder(decoder, one, false);
  }
  const std::array<std::uint8_t, 0U> empty{};
  exercise_decoder(decoder, empty, true);
}

inline void run_frame_extension(Input& input) {
  FrameCodec codec;
  Frame frame;
  frame.type = FrameType::data;
  frame.channel = ChannelId{1U + (input.u32() % 8U), 1U};
  frame.frame_seq = input.u32(1U);
  frame.payload = input.bytes(256U);
  frame.extensions = random_extensions(input, 12U, 64U);
  auto encoded = codec.encode(frame);
  if (encoded) {
    FrameCodec decoder;
    exercise_decoder(decoder, encoded.value(), true);
  }
}

inline void run_frame_header_limit(Input& input) {
  FrameLimits limits;
  limits.max_frame = 1U + input.bounded(2048U);
  limits.max_header = 1U + input.bounded(128U);
  FrameCodec decoder(limits);
  const Bytes raw = input.bytes(2048U);
  exercise_decoder(decoder, raw, input.bit());
}

inline void run_frame_crc_mutation(Input& input) {
  FrameCodec codec;
  Frame frame = random_frame(input, 256U, 16U);
  auto encoded = codec.encode(frame);
  if (!encoded) {
    return;
  }
  Bytes mutated = encoded.take_value();
  if (!mutated.empty()) {
    const std::size_t index = input.bounded(mutated.size() - 1U);
    mutated[index] = static_cast<std::uint8_t>(mutated[index] ^ input.byte(0x5AU));
  }
  FrameCodec decoder;
  exercise_decoder(decoder, mutated, true);
}

inline void run_frame_concat_eof(Input& input) {
  FrameCodec codec;
  Bytes stream;
  for (std::size_t i = 0; i < 8U; ++i) {
    Frame frame = random_frame(input, 128U, 16U);
    auto encoded = codec.encode(frame);
    if (encoded) {
      stream.insert(stream.end(), encoded.value().begin(), encoded.value().end());
    }
  }
  if (!stream.empty() && input.bit()) {
    stream.resize(input.bounded(stream.size() - 1U));
  }
  FrameCodec decoder;
  exercise_decoder(decoder, stream, input.bit());
}

inline void run_hello_payload(Input& input) {
  const Bytes raw = input.bytes(1024U);
  auto decoded = decode_hello_payload(raw);
  if (decoded) {
    (void)encode_hello_payload(decoded.value());
  }
  HelloMessage hello;
  hello.min_major = 1U;
  hello.max_major = static_cast<std::uint16_t>(1U + (input.byte() % 4U));
  hello.limits.max_frame = 64U + input.bounded(kDefaultMaxFrame - 64U);
  hello.limits.max_message = 128U + input.bounded(8192U);
  hello.limits.max_channels = static_cast<std::uint16_t>(1U + input.bounded(255U));
  hello.plugins.push_back(EchoPlugin().descriptor());
  auto encoded = encode_hello_payload(hello);
  if (encoded) {
    (void)decode_hello_payload(encoded.value());
  }
}

inline void run_negotiation_matrix(Input& input) {
  HelloMessage left;
  HelloMessage right;
  left.min_major = 1U;
  left.max_major = static_cast<std::uint16_t>(1U + (input.byte() % 3U));
  right.min_major = static_cast<std::uint16_t>(1U + (input.byte() % 2U));
  right.max_major = static_cast<std::uint16_t>(right.min_major + (input.byte() % 3U));
  left.limits.max_frame = 512U + input.bounded(4096U);
  right.limits.max_frame = 256U + input.bounded(4096U);
  left.limits.max_message = 1024U + input.bounded(32768U);
  right.limits.max_message = 512U + input.bounded(32768U);
  left.limits.max_channels = static_cast<std::uint16_t>(1U + input.bounded(255U));
  right.limits.max_channels = static_cast<std::uint16_t>(1U + input.bounded(255U));
  left.plugins.push_back(EchoPlugin().descriptor());
  const std::uint64_t schema = input.bit() ? EchoPlugin().descriptor().schema_hash : input.u64();
  right.plugins.push_back(PluginDescriptor{7U, schema, 4U});
  (void)negotiate(left, right);
}

inline void run_capability_policy(Input& input) {
  GatewayPolicy policy;
  if (input.bit()) {
    (void)policy.add_translator(7U, [](std::span<const std::uint8_t> payload) {
      return Bytes(payload.begin(), payload.end());
    });
  }
  CapabilitySet from = capability(64U + input.bounded(4096U));
  CapabilitySet to = capability(64U + input.bounded(4096U),
                                input.bit() ? EchoPlugin().descriptor().schema_hash : input.u64());
  const Bytes payload = input.bytes(512U);
  if (policy.may_forward(from, to, 7U) || policy.has_translator(7U)) {
    (void)policy.translate_payload(7U, payload);
  }
}

inline void run_connection_peer_bytes(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  (void)handshake(left, right);
  for (std::size_t i = 0; i < 16U && !input.empty(); ++i) {
    const Bytes chunk = input.bytes(256U);
    if (input.bit()) {
      (void)left.receive(chunk, input.bit());
    } else {
      (void)right.receive(chunk, input.bit());
    }
  }
}

inline void run_connection_chunks(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  auto hello = left.start();
  if (!hello) {
    return;
  }
  FrameCodec splitter;
  for (const Bytes& chunk : hello.value()) {
    std::size_t offset = 0;
    while (offset < chunk.size()) {
      const std::size_t take = std::min<std::size_t>(1U + input.bounded(16U), chunk.size() - offset);
      std::span<const std::uint8_t> part(chunk.data() + static_cast<std::ptrdiff_t>(offset), take);
      (void)right.receive(part, false);
      offset += take;
    }
  }
  (void)splitter;
}

inline void run_connection_open(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  if (!handshake(left, right)) {
    return;
  }
  const std::size_t count = 1U + input.bounded(16U);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t window = 1U + input.bounded(kDefaultConnectionWindow / 8U);
    auto opened = left.open_channel(OpenRequest{input.bit() ? 7U : input.u32(), window});
    if (opened) {
      (void)deliver(right, opened.value().second);
    }
  }
}

inline void run_connection_data_ext(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  if (!handshake(left, right)) {
    return;
  }
  auto channel = open_pair(left, right, 8192U);
  if (!channel) {
    return;
  }
  FrameCodec codec;
  const std::uint32_t total = 1U + static_cast<std::uint32_t>(input.bounded(512U));
  const std::uint32_t offset = static_cast<std::uint32_t>(input.bounded(total - 1U));
  Frame frame;
  frame.type = FrameType::data;
  frame.channel = channel.value();
  frame.frame_seq = input.u32(2U);
  frame.payload = input.bytes(std::min<std::size_t>(128U, total - offset));
  frame.extensions = data_extensions(input.u32(1U), offset, total, 7U);
  auto encoded = codec.encode(frame);
  if (encoded) {
    (void)right.receive(encoded.value(), input.bit());
  }
}

inline void run_connection_credit_ack(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  if (!handshake(left, right)) {
    return;
  }
  auto channel = open_pair(left, right, 1024U + input.bounded(4096U));
  if (!channel) {
    return;
  }
  for (std::size_t i = 0; i < 12U; ++i) {
    if (input.bit()) {
      auto credit = right.grant_credit(channel.value(), input.bounded(8192U));
      if (credit) {
        (void)deliver(left, credit.value());
      }
    } else {
      std::vector<AckRange> ranges;
      const std::size_t count = input.bounded(4U);
      for (std::size_t r = 0; r < count; ++r) {
        const std::uint32_t first = input.u32(1U);
        ranges.push_back(AckRange{first, first + static_cast<std::uint32_t>(input.bounded(8U))});
      }
      auto ack = left.acknowledge(std::move(ranges));
      if (ack) {
        (void)deliver(right, ack.value());
      }
    }
  }
}

inline void run_connection_resume(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  if (!handshake(left, right)) {
    return;
  }
  ResumeRequest request;
  if (left.capabilities().has_value() && input.bit()) {
    request.transcript_hash = left.capabilities().value().transcript_hash;
  } else {
    const Bytes hash = input.fixed_bytes(request.transcript_hash.size());
    std::copy(hash.begin(), hash.end(), request.transcript_hash.begin());
  }
  request.epoch = static_cast<std::uint16_t>(1U + input.bounded(3U));
  request.first_required_seq = input.u32();
  auto resume = right.resume(request);
  if (resume) {
    (void)deliver(left, resume.value());
  }
}

inline void run_connection_timer(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  (void)handshake(left, right);
  std::uint64_t now = 0U;
  for (std::size_t i = 0; i < 16U; ++i) {
    now += 1U + input.bounded(10000U);
    auto produced = left.advance_time(now);
    if (produced) {
      (void)deliver(right, produced.value());
    }
  }
}

inline void run_connection_reset_goaway(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  if (!handshake(left, right)) {
    return;
  }
  auto channel = open_pair(left, right, 4096U);
  if (channel) {
    if (input.bit()) {
      auto half = left.half_close(channel.value(), input.bit() ? Direction::local_send
                                                               : Direction::remote_send);
      if (half) {
        (void)deliver(right, half.value());
      }
    }
    auto reset = input.bit() ? left.reset(channel.value()) : right.reset(channel.value());
    if (reset) {
      (void)deliver(input.bit() ? right : left, reset.value());
    }
  }
  auto goaway = left.goaway();
  if (goaway) {
    (void)deliver(right, goaway.value());
  }
}

inline void run_connection_outbound(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  if (!handshake(left, right)) {
    return;
  }
  auto channel = open_pair(left, right, 8192U);
  if (!channel) {
    return;
  }
  for (std::size_t i = 0; i < 8U; ++i) {
    auto sent = left.send(channel.value(), input.bytes(256U));
    if (sent) {
      for (const Bytes& frame : sent.value()) {
        (void)frame;
      }
      auto flushed = left.flush_outbound(1U + input.bounded(512U));
      (void)deliver(right, flushed);
    }
  }
}

inline void run_connection_multichannel(Input& input) {
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry());
  if (!handshake(left, right)) {
    return;
  }
  std::vector<ChannelId> channels;
  for (std::size_t i = 0; i < 12U; ++i) {
    auto channel = open_pair(left, right, 2048U);
    if (channel) {
      channels.push_back(channel.value());
    }
  }
  for (ChannelId channel : channels) {
    auto sent = left.send(channel, input.bytes(128U));
    if (sent) {
      (void)deliver(right, sent.value());
    }
  }
}

inline void run_connection_event_script(Input& input) {
  DeterministicExecutor executor(32U, 4U);
  ConnectionEngine left(LocalPolicy{}, make_registry());
  ConnectionEngine right(LocalPolicy{}, make_registry(), &executor);
  (void)handshake(left, right);
  std::optional<ChannelId> channel;
  for (std::size_t i = 0; i < 32U; ++i) {
    switch (input.byte() % 6U) {
      case 0U:
        channel = open_pair(left, right, 4096U);
        break;
      case 1U:
        if (channel) {
          auto sent = left.send(channel.value(), input.bytes(64U));
          if (sent) {
            (void)deliver(right, sent.value());
          }
        }
        break;
      case 2U:
        (void)left.ping(input.u64());
        break;
      case 3U:
        if (channel) {
          (void)left.reset(channel.value());
        }
        break;
      case 4U:
        (void)right.receive(input.bytes(64U), input.bit());
        break;
      default:
        drain_executor(executor);
        break;
    }
  }
  drain_executor(executor);
  (void)event_digest(left);
  (void)event_digest(right);
}

inline void run_channel_table(Input& input) {
  ChannelTable table(static_cast<std::uint16_t>(1U + input.bounded(64U)),
                     1U + input.bounded(4096U), 1U + input.bounded(8192U));
  std::vector<ChannelId> ids;
  for (std::size_t i = 0; i < 64U; ++i) {
    switch (input.byte() % 6U) {
      case 0U: {
        auto id = table.allocate();
        if (id) {
          ids.push_back(id.value());
        }
        break;
      }
      case 1U:
        if (!ids.empty()) {
          (void)table.activate(ids[input.bounded(ids.size() - 1U)]);
        }
        break;
      case 2U:
        (void)table.accept_remote(channel_from(input));
        break;
      case 3U:
        if (!ids.empty()) {
          (void)table.reset(ids[input.bounded(ids.size() - 1U)]);
        }
        break;
      case 4U:
        if (!ids.empty()) {
          (void)table.half_close(ids[input.bounded(ids.size() - 1U)],
                                 input.bit() ? Direction::local_send : Direction::remote_send);
        }
        break;
      default:
        if (!ids.empty()) {
          (void)table.retire_tombstone(ids[input.bounded(ids.size() - 1U)]);
        }
        break;
    }
    (void)table.live_count();
  }
}

inline void run_reassembler_ranges(Input& input) {
  Reassembler reassembler(1U + input.bounded(4096U));
  ChannelId channel{1U, 1U};
  for (std::size_t i = 0; i < 32U; ++i) {
    const std::uint32_t sequence = 1U + static_cast<std::uint32_t>(input.bounded(8U));
    const std::uint32_t total = 1U + static_cast<std::uint32_t>(input.bounded(1024U));
    const std::uint32_t offset = static_cast<std::uint32_t>(input.bounded(total - 1U));
    Bytes payload = input.bytes(std::min<std::size_t>(128U, total - offset));
    (void)reassembler.insert(channel, sequence, offset, total, payload);
    if (input.bit()) {
      reassembler.reset_message(sequence);
    }
  }
}

inline void run_reassembler_overlap(Input& input) {
  Reassembler reassembler(2048U);
  ChannelId channel{1U + (input.u32() % 4U), 1U};
  const std::uint32_t sequence = 1U + static_cast<std::uint32_t>(input.bounded(4U));
  const std::uint32_t total = 32U + static_cast<std::uint32_t>(input.bounded(512U));
  const Bytes first = input.fixed_bytes(std::min<std::size_t>(64U, total));
  (void)reassembler.insert(channel, sequence, 0U, total, first);
  for (std::size_t i = 0; i < 16U; ++i) {
    const std::uint32_t offset = static_cast<std::uint32_t>(input.bounded(total - 1U));
    const Bytes fragment = input.bytes(std::min<std::size_t>(96U, total - offset));
    (void)reassembler.insert(channel, sequence, offset, total, fragment);
  }
}

inline void run_flow_account(Input& input) {
  FlowAccount account(input.bounded(65535U));
  for (std::size_t i = 0; i < 64U; ++i) {
    const std::size_t amount = input.bounded(8192U);
    switch (input.byte() % 3U) {
      case 0U:
        (void)account.reserve(amount);
        break;
      case 1U:
        (void)account.release(amount);
        break;
      default:
        (void)account.grant(amount);
        break;
    }
    (void)account.invariant_holds();
  }
}

inline void run_replay_window(Input& input) {
  ReplayWindow replay(1U + input.bounded(64U));
  for (std::size_t i = 0; i < 64U; ++i) {
    switch (input.byte() % 4U) {
      case 0U:
        (void)replay.record(1U, input.u32(), input.bytes(128U));
        break;
      case 1U: {
        std::vector<AckRange> ranges;
        const std::size_t count = input.bounded(4U);
        for (std::size_t r = 0; r < count; ++r) {
          const std::uint32_t first = input.u32();
          ranges.push_back(AckRange{first, first + static_cast<std::uint32_t>(input.bounded(16U))});
        }
        (void)replay.acknowledge(1U, std::move(ranges));
        break;
      }
      case 2U:
        (void)replay.retained_from(1U, input.u32());
        break;
      default:
        (void)replay.due_for_retry(static_cast<std::uint8_t>(input.bounded(8U)));
        break;
    }
  }
  (void)replay.serialize_retained();
}

inline void run_replay_snapshot(Input& input) {
  const std::string text = input.text(2048U);
  auto restored = ReplayWindow::restore_retained(text);
  if (restored) {
    (void)restored.value().serialize_retained();
  }
  ReplayWindow replay(32U);
  for (std::size_t i = 0; i < 16U; ++i) {
    (void)replay.record(1U, static_cast<std::uint32_t>(i + 1U), input.bytes(64U));
  }
  auto serialized = replay.serialize_retained();
  if (serialized) {
    (void)ReplayWindow::restore_retained(serialized.value());
  }
}

inline void run_ack_payload(Input& input) {
  const Bytes raw = input.bytes(512U);
  auto decoded = decode_ack_payload(raw);
  if (decoded) {
    (void)encode_ack_payload(decoded.value().first, decoded.value().second);
  }
  std::vector<AckRange> ranges;
  for (std::size_t i = 0; i < input.bounded(16U); ++i) {
    const std::uint32_t first = input.u32();
    ranges.push_back(AckRange{first, first + static_cast<std::uint32_t>(input.bounded(32U))});
  }
  (void)encode_ack_payload(static_cast<std::uint16_t>(1U + input.bounded(4U)), std::move(ranges));
}

inline void run_timer_wheel(Input& input) {
  TimerWheel wheel;
  std::vector<TimerId> ids;
  for (std::size_t i = 0; i < 64U; ++i) {
    switch (input.byte() % 3U) {
      case 0U:
        ids.push_back(wheel.schedule(static_cast<TimerKind>(input.byte() % 6U),
                                     channel_from(input), input.u64()));
        break;
      case 1U:
        if (!ids.empty()) {
          (void)wheel.cancel(ids[input.bounded(ids.size() - 1U)]);
        }
        break;
      default:
        (void)wheel.expire(input.u64());
        break;
    }
  }
}

inline void run_trace_parse(Input& input) {
  auto parsed = TraceLog::parse(input.text(4096U));
  if (parsed) {
    (void)verify_trace_replay(parsed.value());
    (void)parsed.value().serialize();
  }
}

inline TraceKind trace_kind_from(Input& input) {
  switch (input.byte() % 5U) {
    case 0U:
      return TraceKind::transport_bytes;
    case 1U:
      return TraceKind::api_event;
    case 2U:
      return TraceKind::timer;
    case 3U:
      return TraceKind::plugin_completion;
    default:
      return TraceKind::diagnostic;
  }
}

inline void run_trace_build(Input& input) {
  TraceLog log;
  const std::size_t count = input.bounded(32U);
  for (std::size_t i = 0; i < count; ++i) {
    TraceEvent event;
    event.time_ms = input.u64();
    event.kind = trace_kind_from(input);
    event.label = input.text(32U);
    event.bytes = input.bytes(128U);
    log.record(std::move(event));
  }
  auto serialized = log.serialize();
  if (serialized) {
    auto parsed = TraceLog::parse(serialized.value());
    if (parsed) {
      (void)verify_trace_replay(parsed.value());
    }
  }
}

inline OutboundItem outbound_item_from(Input& input, OutboundPriority priority) {
  OutboundItem item;
  item.priority = priority;
  item.channel = channel_from(input);
  item.sequence = input.u32();
  item.encoded = input.bytes(256U);
  return item;
}

inline void run_scheduler_mixed(Input& input) {
  OutboundScheduler scheduler(1U + input.bounded(8192U));
  for (std::size_t i = 0; i < 64U; ++i) {
    if (input.bit()) {
      (void)scheduler.enqueue(outbound_item_from(
          input, input.bit() ? OutboundPriority::control : OutboundPriority::data));
    } else {
      (void)scheduler.drain(1U + input.bounded(512U));
    }
    (void)scheduler.empty();
  }
}

inline void run_scheduler_backpressure(Input& input) {
  OutboundScheduler scheduler(4096U);
  for (std::size_t i = 0; i < 24U; ++i) {
    OutboundItem item;
    item.priority = OutboundPriority::data;
    item.channel = ChannelId{1U + static_cast<std::uint32_t>(i % 4U), 1U};
    item.sequence = static_cast<std::uint32_t>(i + 1U);
    item.encoded = input.fixed_bytes(1U + input.bounded(96U));
    (void)scheduler.enqueue(std::move(item));
  }
  for (std::size_t i = 0; i < 128U && !scheduler.empty(); ++i) {
    (void)scheduler.drain(1U + input.bounded(16U));
  }
}

inline void run_scheduler_order(Input& input) {
  OutboundScheduler scheduler(16384U);
  const std::uint32_t channel_count = 1U + static_cast<std::uint32_t>(input.bounded(8U));
  for (std::size_t i = 0; i < 48U; ++i) {
    OutboundItem item;
    item.priority = input.bit() ? OutboundPriority::control : OutboundPriority::data;
    item.channel = ChannelId{1U + (input.u32() % channel_count), 1U};
    item.sequence = static_cast<std::uint32_t>(1U + input.bounded(16U));
    item.encoded = input.bytes(128U);
    (void)scheduler.enqueue(std::move(item));
  }
  for (std::size_t i = 0; i < 32U; ++i) {
    (void)scheduler.drain(1U + input.bounded(1024U));
  }
}

inline void run_gateway_policy(Input& input) {
  Gateway gateway;
  if (input.bit()) {
    (void)gateway.add_translator(7U, [](std::span<const std::uint8_t> payload) {
      Bytes out(payload.begin(), payload.end());
      std::reverse(out.begin(), out.end());
      return out;
    });
  }
  CapabilitySet from = capability(1U + input.bounded(2048U));
  CapabilitySet to = capability(1U + input.bounded(2048U),
                                input.bit() ? EchoPlugin().descriptor().schema_hash : input.u64());
  (void)gateway.forward(from, to,
                        GatewayRoute{1U, ChannelId{1U, 1U}, ChannelId{2U, 1U}, 7U},
                        input.bytes(512U));
  (void)gateway.translate(from, to, 7U, input.bytes(512U));
}

inline void run_gateway_route(Input& input) {
  Gateway gateway;
  std::vector<ChannelId> sources;
  for (std::size_t i = 0; i < 16U; ++i) {
    ChannelId source{1U + static_cast<std::uint32_t>(i), 1U};
    ChannelId destination{100U + static_cast<std::uint32_t>(i), 1U};
    auto route = gateway.create_route(source, destination, 7U);
    if (route) {
      sources.push_back(source);
    }
  }
  CapabilitySet caps = capability();
  for (ChannelId source : sources) {
    (void)gateway.bridge_message(caps, caps, source, input.bytes(128U));
    (void)gateway.find_route(source);
  }
}

inline void run_gateway_bridge(Input& input) {
  ConnectionEngine client(LocalPolicy{}, make_registry());
  ConnectionEngine ingress(LocalPolicy{}, make_registry());
  ConnectionEngine egress(LocalPolicy{}, make_registry());
  ConnectionEngine server(LocalPolicy{}, make_registry());
  if (!handshake(client, ingress) || !handshake(egress, server)) {
    return;
  }
  auto source = open_pair(client, ingress, 8192U);
  auto destination = open_pair(egress, server, 8192U);
  if (!source || !destination) {
    return;
  }
  Gateway gateway;
  (void)gateway.create_route(source.value(), destination.value(), 7U);
  auto sent = client.send(source.value(), input.bytes(512U));
  if (sent) {
    (void)deliver(ingress, sent.value());
  }
  for (const ConnectionEvent& event : ingress.events()) {
    if (event.kind == ConnectionEvent::Kind::message_delivered && event.channel == source.value()) {
      auto forwarded = gateway.bridge_to_connection(ingress.capabilities().value(),
                                                    egress.capabilities().value(), egress,
                                                    source.value(), event.payload);
      if (forwarded) {
        (void)deliver(server, forwarded.value());
      }
    }
  }
  (void)event_digest(server);
}

inline void run_plugin_registry(Input& input) {
  PluginRegistry registry;
  for (std::size_t i = 0; i < 32U; ++i) {
    const std::uint32_t family = input.bit() ? 7U : (1U + input.u32());
    PluginDescriptor descriptor{family, input.bit() ? EchoPlugin().descriptor().schema_hash
                                                    : input.u64(),
                                static_cast<std::uint32_t>(1U + input.bounded(32U))};
    switch (input.byte() % 4U) {
      case 0U:
        (void)registry.register_factory(descriptor, [] {
          return std::make_unique<EchoPlugin>();
        });
        break;
      case 1U:
        (void)registry.create(family);
        break;
      case 2U:
        (void)registry.create_lease(family);
        break;
      default:
        (void)registry.unregister_family(family);
        break;
    }
    (void)registry.descriptors();
  }
}

inline void run_executor_queue(Input& input) {
  DeterministicExecutor executor(1U + input.bounded(64U), 1U + static_cast<std::uint32_t>(input.bounded(8U)));
  std::vector<std::uint64_t> ids;
  for (std::size_t i = 0; i < 64U; ++i) {
    switch (input.byte() % 3U) {
      case 0U: {
        auto submitted = executor.submit(static_cast<std::uint32_t>(input.bounded(7U)), [] {
          return Result<void>{};
        });
        if (submitted) {
          ids.push_back(submitted.value());
        }
        break;
      }
      case 1U:
        if (!ids.empty()) {
          (void)executor.cancel(ids[input.bounded(ids.size() - 1U)]);
        }
        break;
      default:
        (void)executor.drain_one();
        break;
    }
  }
  (void)executor.drain_all();
}

inline void run_threaded_executor(Input& input) {
  ThreadedExecutor executor(8U, 1U + static_cast<std::uint32_t>(input.bounded(4U)));
  std::atomic<std::uint32_t> observed{0U};
  const std::size_t count = 1U + input.bounded(8U);
  for (std::size_t i = 0; i < count; ++i) {
    (void)executor.submit(static_cast<std::uint32_t>(input.bounded(3U)), [&observed] {
      observed.fetch_add(1U, std::memory_order_relaxed);
      return Result<void>{};
    });
  }
  (void)executor.wait_idle();
  if (input.bit()) {
    (void)executor.shutdown();
  }
  (void)observed.load(std::memory_order_relaxed);
}

inline void run_memory_transport(Input& input) {
  MemoryPipe pipe;
  for (std::size_t i = 0; i < 64U; ++i) {
    switch (input.byte() % 4U) {
      case 0U:
        pipe.left_to_right.write(input.bytes(256U));
        break;
      case 1U:
        pipe.right_to_left.write(input.bytes(256U));
        break;
      case 2U:
        (void)pipe.left_to_right.read();
        (void)pipe.right_to_left.read();
        break;
      default:
        if (input.bit()) {
          pipe.left_to_right.close();
        } else {
          pipe.right_to_left.close();
        }
        break;
    }
    (void)pipe.left_to_right.queued_bytes();
    (void)pipe.right_to_left.queued_bytes();
  }
}

}  // namespace lattice::fuzz
