#include "lattice/connection.hpp"

#include <algorithm>

namespace lattice {
namespace {

constexpr std::uint16_t kExtMessageSeq = 1U;
constexpr std::uint16_t kExtFragmentOffset = 2U;
constexpr std::uint16_t kExtMessageTotal = 3U;
constexpr std::uint16_t kExtFamilyId = 4U;

void append_u32(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

Bytes u32_bytes(std::uint32_t value) {
  Bytes out;
  append_u32(out, value);
  return out;
}

[[nodiscard]] Error state_error(std::string detail) {
  return make_error(ErrorScope::connection, ErrorCode::illegal_state,
                    CloseAction::close_connection, std::move(detail));
}

}  // namespace

ConnectionEngine::ConnectionEngine(LocalPolicy policy, PluginRegistry registry)
    : policy_(policy),
      registry_(std::move(registry)),
      codec_(FrameLimits{policy.max_frame, 4096U}),
      replay_(policy.replay_window) {}

Result<std::vector<Bytes>> ConnectionEngine::start() {
  if (state_ != ConnectionState::created) {
    return state_error("connection already started");
  }
  HelloMessage hello;
  hello.min_major = policy_.min_major;
  hello.max_major = policy_.max_major;
  hello.limits = policy_;
  hello.plugins = registry_.descriptors();
  auto payload = encode_hello_payload(hello);
  if (!payload) {
    return payload.error();
  }
  state_ = ConnectionState::negotiating;
  auto frame = make_frame(FrameType::hello, ChannelId{}, payload.take_value());
  if (!frame) {
    return frame.error();
  }
  return emit(frame.take_value());
}

Result<std::vector<Bytes>> ConnectionEngine::receive(std::span<const std::uint8_t> bytes, bool eof) {
  std::vector<Bytes> outbound;
  for (DecodeEvent& event : codec_.feed(bytes, eof)) {
    if (event.status == DecodeStatus::need_more) {
      continue;
    }
    if (event.status == DecodeStatus::error) {
      diagnostic(event.error.value());
      state_ = ConnectionState::closed;
      return event.error.value();
    }
    auto produced = handle_frame(event.frame.value());
    if (!produced) {
      diagnostic(produced.error());
      if (produced.error().action == CloseAction::close_connection) {
        state_ = ConnectionState::closed;
      }
      return produced.error();
    }
    outbound.insert(outbound.end(), produced.value().begin(), produced.value().end());
  }
  return outbound;
}

Result<std::pair<ChannelId, std::vector<Bytes>>> ConnectionEngine::open_channel(OpenRequest request) {
  if (state_ != ConnectionState::active || !channels_.has_value()) {
    return state_error("OPEN before negotiated Active state");
  }
  const bool plugin_ok = std::any_of(capabilities_->plugins.begin(), capabilities_->plugins.end(),
                                    [request](const PluginDescriptor& descriptor) {
                                      return descriptor.family_id == request.family_id;
                                    });
  if (!plugin_ok) {
    return make_error(ErrorScope::plugin, ErrorCode::plugin_decode, CloseAction::reject_message,
                      "requested plugin family was not negotiated");
  }
  auto id = channels_->allocate();
  if (!id) {
    return id.error();
  }
  auto active = channels_->activate(id.value());
  if (!active) {
    return active.error();
  }
  Bytes payload;
  append_u32(payload, request.family_id);
  append_u32(payload, static_cast<std::uint32_t>(request.initial_window));
  auto frame = make_frame(FrameType::open, id.value(), std::move(payload));
  if (!frame) {
    return frame.error();
  }
  auto bytes = emit(frame.take_value());
  if (!bytes) {
    return bytes.error();
  }
  events_.push_back(ConnectionEvent{ConnectionEvent::Kind::channel_opened, id.value(), 0U, {}, std::nullopt});
  return std::make_pair(id.value(), bytes.take_value());
}

Result<std::vector<Bytes>> ConnectionEngine::send(ChannelId id, std::span<const std::uint8_t> payload) {
  if (state_ != ConnectionState::active || !capabilities_ || !channels_) {
    return state_error("DATA before negotiated Active state");
  }
  ChannelSlot* slot = channels_->find(id);
  if (slot == nullptr) {
    return make_error(ErrorScope::channel, ErrorCode::stale_generation,
                      CloseAction::reset_channel, "send targets stale channel");
  }
  if (slot->local_half_closed) {
    return make_error(ErrorScope::channel, ErrorCode::illegal_state,
                      CloseAction::reset_channel, "DATA after local half-close");
  }
  if (payload.size() > capabilities_->max_message) {
    return make_error(ErrorScope::message, ErrorCode::resource_limit,
                      CloseAction::reject_message, "payload exceeds negotiated message cap");
  }
  auto reserve = slot->flow.reserve(payload.size());
  if (!reserve) {
    return reserve.error();
  }
  std::vector<Bytes> out;
  const std::uint32_t sequence = slot->next_send_seq++;
  const std::size_t max_fragment = std::max<std::size_t>(1U, capabilities_->max_frame / 2U);
  for (std::size_t offset = 0; offset < payload.size(); offset += max_fragment) {
    const std::size_t take = std::min(max_fragment, payload.size() - offset);
    Bytes fragment(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                   payload.begin() + static_cast<std::ptrdiff_t>(offset + take));
    std::vector<Extension> extensions{
        Extension{kExtMessageSeq, true, u32_bytes(sequence)},
        Extension{kExtFragmentOffset, true, u32_bytes(static_cast<std::uint32_t>(offset))},
        Extension{kExtMessageTotal, true, u32_bytes(static_cast<std::uint32_t>(payload.size()))},
        Extension{kExtFamilyId, true, u32_bytes(7U)}};
    auto frame = make_frame(FrameType::data, id, std::move(fragment), std::move(extensions));
    if (!frame) {
      (void)slot->flow.release(payload.size());
      return frame.error();
    }
    auto encoded = emit(frame.take_value());
    if (!encoded) {
      (void)slot->flow.release(payload.size());
      return encoded.error();
    }
    out.insert(out.end(), encoded.value().begin(), encoded.value().end());
  }
  (void)slot->flow.release(payload.size());
  return out;
}

Result<std::vector<Bytes>> ConnectionEngine::grant_credit(ChannelId id, std::size_t amount) {
  Bytes payload;
  append_u32(payload, static_cast<std::uint32_t>(amount));
  auto frame = make_frame(FrameType::credit, id, std::move(payload));
  if (!frame) {
    return frame.error();
  }
  return emit(frame.take_value());
}

Result<std::vector<Bytes>> ConnectionEngine::half_close(ChannelId id, Direction direction) {
  if (!channels_) {
    return state_error("half-close before channel table exists");
  }
  auto close = channels_->half_close(id, direction);
  if (!close) {
    return close.error();
  }
  Bytes payload{static_cast<std::uint8_t>(direction == Direction::local_send ? 0U : 1U)};
  auto frame = make_frame(FrameType::half_close, id, std::move(payload));
  if (!frame) {
    return frame.error();
  }
  events_.push_back(ConnectionEvent{ConnectionEvent::Kind::half_closed, id, 0U, {}, std::nullopt});
  return emit(frame.take_value());
}

Result<std::vector<Bytes>> ConnectionEngine::reset(ChannelId id) {
  if (!channels_) {
    return state_error("reset before channel table exists");
  }
  auto reset = channels_->reset(id);
  if (!reset) {
    return reset.error();
  }
  auto frame = make_frame(FrameType::reset, id, {});
  if (!frame) {
    return frame.error();
  }
  events_.push_back(ConnectionEvent{ConnectionEvent::Kind::channel_reset, id, 0U, {}, std::nullopt});
  return emit(frame.take_value());
}

Result<std::vector<Bytes>> ConnectionEngine::goaway() {
  state_ = ConnectionState::draining;
  auto frame = make_frame(FrameType::goaway, ChannelId{}, {});
  if (!frame) {
    return frame.error();
  }
  return emit(frame.take_value());
}

Result<Frame> ConnectionEngine::make_frame(FrameType type, ChannelId channel, Bytes payload,
                                           std::vector<Extension> extensions) {
  Frame frame;
  frame.type = type;
  frame.channel = channel;
  frame.frame_seq = next_frame_seq_++;
  frame.payload = std::move(payload);
  frame.extensions = std::move(extensions);
  return frame;
}

Result<std::vector<Bytes>> ConnectionEngine::handle_frame(const Frame& frame) {
  switch (frame.type) {
    case FrameType::hello: return handle_hello(frame);
    case FrameType::open: return handle_open(frame);
    case FrameType::data: return handle_data(frame);
    case FrameType::credit: return handle_credit(frame);
    case FrameType::half_close: {
      if (!channels_ || frame.payload.size() != 1U) {
        return make_error(ErrorScope::channel, ErrorCode::illegal_state,
                          CloseAction::reset_channel, "malformed HALF_CLOSE");
      }
      const Direction direction = frame.payload[0] == 0U ? Direction::remote_send : Direction::local_send;
      auto close = channels_->half_close(frame.channel, direction);
      if (!close) {
        return close.error();
      }
      events_.push_back(ConnectionEvent{ConnectionEvent::Kind::half_closed, frame.channel, 0U, {}, std::nullopt});
      return std::vector<Bytes>{};
    }
    case FrameType::reset: {
      if (channels_) {
        (void)channels_->reset(frame.channel);
      }
      events_.push_back(ConnectionEvent{ConnectionEvent::Kind::channel_reset, frame.channel, 0U, {}, std::nullopt});
      return std::vector<Bytes>{};
    }
    case FrameType::goaway:
      state_ = ConnectionState::draining;
      events_.push_back(ConnectionEvent{ConnectionEvent::Kind::closed, frame.channel, 0U, {}, std::nullopt});
      return std::vector<Bytes>{};
    case FrameType::ack:
    case FrameType::ping:
    case FrameType::pong:
    case FrameType::resume:
      return std::vector<Bytes>{};
  }
  return make_error(ErrorScope::connection, ErrorCode::unsupported_frame_type,
                    CloseAction::close_connection, "unhandled frame type");
}

Result<std::vector<Bytes>> ConnectionEngine::handle_hello(const Frame& frame) {
  if (state_ != ConnectionState::negotiating) {
    return make_error(ErrorScope::connection, ErrorCode::illegal_state,
                      CloseAction::close_connection, "unexpected HELLO");
  }
  auto peer = decode_hello_payload(frame.payload);
  if (!peer) {
    return peer.error();
  }
  HelloMessage local;
  local.min_major = policy_.min_major;
  local.max_major = policy_.max_major;
  local.limits = policy_;
  local.plugins = registry_.descriptors();
  auto caps = negotiate(local, peer.value());
  if (!caps) {
    return caps.error();
  }
  capabilities_ = caps.value();
  channels_.emplace(capabilities_->max_channels, capabilities_->connection_window / capabilities_->max_channels,
                    capabilities_->max_message);
  state_ = ConnectionState::active;
  events_.push_back(ConnectionEvent{ConnectionEvent::Kind::negotiated, ChannelId{}, 0U, {}, std::nullopt});
  return std::vector<Bytes>{};
}

Result<std::vector<Bytes>> ConnectionEngine::handle_open(const Frame& frame) {
  if (state_ != ConnectionState::active || !channels_ || frame.payload.size() != 8U) {
    return make_error(ErrorScope::connection, ErrorCode::illegal_state,
                      CloseAction::close_connection, "OPEN outside Active state or malformed");
  }
  auto family = read_u32_be(frame.payload, 0);
  if (!family) {
    return family.error();
  }
  const bool plugin_ok = std::any_of(capabilities_->plugins.begin(), capabilities_->plugins.end(),
                                    [&](const PluginDescriptor& descriptor) {
                                      return descriptor.family_id == family.value();
                                    });
  if (!plugin_ok) {
    return make_error(ErrorScope::plugin, ErrorCode::plugin_decode, CloseAction::reset_channel,
                      "OPEN requested unnegotiated plugin family");
  }
  auto accepted = channels_->accept_remote(frame.channel);
  if (!accepted) {
    return accepted.error();
  }
  events_.push_back(ConnectionEvent{ConnectionEvent::Kind::channel_opened, frame.channel, 0U, {}, std::nullopt});
  return std::vector<Bytes>{};
}

Result<std::vector<Bytes>> ConnectionEngine::handle_data(const Frame& frame) {
  if (state_ != ConnectionState::active || !channels_) {
    return make_error(ErrorScope::connection, ErrorCode::illegal_state,
                      CloseAction::close_connection, "DATA outside Active state");
  }
  ChannelSlot* slot = channels_->find(frame.channel);
  if (slot == nullptr) {
    return make_error(ErrorScope::channel, ErrorCode::stale_generation, CloseAction::reset_channel,
                      "DATA targets stale generation");
  }
  if (slot->remote_half_closed) {
    return make_error(ErrorScope::channel, ErrorCode::illegal_state, CloseAction::reset_channel,
                      "DATA after peer half-close");
  }
  auto seq = extension_u32(frame, kExtMessageSeq);
  auto offset = extension_u32(frame, kExtFragmentOffset);
  auto total = extension_u32(frame, kExtMessageTotal);
  auto family = extension_u32(frame, kExtFamilyId);
  if (!seq || !offset || !total || !family) {
    return make_error(ErrorScope::message, ErrorCode::malformed_tlv, CloseAction::reset_channel,
                      "DATA missing required fragment extensions");
  }
  if (seq.value() < slot->next_recv_seq) {
    return make_error(ErrorScope::message, ErrorCode::sequence_error, CloseAction::reset_channel,
                      "DATA message sequence regressed");
  }
  auto reserve = slot->flow.reserve(frame.payload.size());
  if (!reserve) {
    return reserve.error();
  }
  auto message = slot->reassembler.insert(frame.channel, seq.value(), offset.value(), total.value(),
                                          frame.payload);
  (void)slot->flow.release(frame.payload.size());
  if (!message) {
    return message.error();
  }
  if (!message.value().has_value()) {
    return std::vector<Bytes>{};
  }
  LogicalMessage completed = std::move(message.value().value());
  if (completed.sequence != slot->next_recv_seq) {
    return make_error(ErrorScope::message, ErrorCode::sequence_error, CloseAction::reset_channel,
                      "DATA completed out of order");
  }
  slot->next_recv_seq++;
  events_.push_back(ConnectionEvent{ConnectionEvent::Kind::message_delivered, frame.channel,
                                    completed.sequence, completed.payload, std::nullopt});
  auto plugin = registry_.create(family.value());
  if (!plugin) {
    return plugin.error();
  }
  auto response = plugin.value()->handle(completed);
  if (!response) {
    return response.error();
  }
  events_.push_back(ConnectionEvent{ConnectionEvent::Kind::plugin_response, frame.channel,
                                    completed.sequence, response.value(), std::nullopt});
  return std::vector<Bytes>{};
}

Result<std::vector<Bytes>> ConnectionEngine::handle_credit(const Frame& frame) {
  if (!channels_ || frame.payload.size() != 4U) {
    return make_error(ErrorScope::channel, ErrorCode::illegal_state,
                      CloseAction::reset_channel, "malformed CREDIT");
  }
  auto amount = read_u32_be(frame.payload, 0);
  if (!amount) {
    return amount.error();
  }
  ChannelSlot* slot = channels_->find(frame.channel);
  if (slot == nullptr) {
    return make_error(ErrorScope::channel, ErrorCode::stale_generation,
                      CloseAction::reset_channel, "CREDIT targets stale channel");
  }
  auto grant = slot->flow.grant(amount.value());
  if (!grant) {
    return grant.error();
  }
  return std::vector<Bytes>{};
}

Result<std::vector<Bytes>> ConnectionEngine::emit(Frame frame) {
  auto encoded = codec_.encode(frame);
  if (!encoded) {
    return encoded.error();
  }
  auto record = replay_.record(1U, frame.frame_seq, encoded.value());
  if (!record) {
    return record.error();
  }
  return std::vector<Bytes>{encoded.take_value()};
}

void ConnectionEngine::diagnostic(Error error) {
  events_.push_back(ConnectionEvent{ConnectionEvent::Kind::diagnostic, ChannelId{}, 0U, {}, std::move(error)});
}

}  // namespace lattice
