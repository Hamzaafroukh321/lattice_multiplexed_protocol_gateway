#pragma once

#include "lattice/channel.hpp"
#include "lattice/plugin.hpp"
#include "lattice/replay.hpp"
#include "lattice/transport.hpp"

#include <optional>

namespace lattice {

struct HelloMessage {
  std::uint16_t min_major{1};
  std::uint16_t max_major{1};
  LocalPolicy limits;
  std::vector<PluginDescriptor> plugins;
};

[[nodiscard]] Result<Bytes> encode_hello_payload(const HelloMessage& hello);
[[nodiscard]] Result<HelloMessage> decode_hello_payload(std::span<const std::uint8_t> payload);
[[nodiscard]] Result<CapabilitySet> negotiate(const HelloMessage& local,
                                              const HelloMessage& peer);

struct OpenRequest {
  std::uint32_t family_id{7};
  std::size_t initial_window{kDefaultConnectionWindow / 4U};
};

struct ConnectionEvent {
  enum class Kind : std::uint8_t {
    negotiated,
    channel_opened,
    message_delivered,
    plugin_response,
    channel_reset,
    half_closed,
    closed,
    diagnostic
  };

  Kind kind{Kind::diagnostic};
  ChannelId channel;
  std::uint32_t sequence{0};
  Bytes payload;
  std::optional<Error> error;
};

class ConnectionEngine {
 public:
  ConnectionEngine(LocalPolicy policy, PluginRegistry registry);

  [[nodiscard]] ConnectionState state() const { return state_; }
  [[nodiscard]] const std::optional<CapabilitySet>& capabilities() const { return capabilities_; }
  [[nodiscard]] const std::vector<ConnectionEvent>& events() const { return events_; }

  [[nodiscard]] Result<std::vector<Bytes>> start();
  [[nodiscard]] Result<std::vector<Bytes>> receive(std::span<const std::uint8_t> bytes, bool eof);
  [[nodiscard]] Result<std::pair<ChannelId, std::vector<Bytes>>> open_channel(OpenRequest request);
  [[nodiscard]] Result<std::vector<Bytes>> send(ChannelId id, std::span<const std::uint8_t> payload);
  [[nodiscard]] Result<std::vector<Bytes>> grant_credit(ChannelId id, std::size_t amount);
  [[nodiscard]] Result<std::vector<Bytes>> half_close(ChannelId id, Direction direction);
  [[nodiscard]] Result<std::vector<Bytes>> reset(ChannelId id);
  [[nodiscard]] Result<std::vector<Bytes>> goaway();

 private:
  [[nodiscard]] Result<Frame> make_frame(FrameType type, ChannelId channel, Bytes payload,
                                         std::vector<Extension> extensions = {});
  [[nodiscard]] Result<std::vector<Bytes>> handle_frame(const Frame& frame);
  [[nodiscard]] Result<std::vector<Bytes>> handle_hello(const Frame& frame);
  [[nodiscard]] Result<std::vector<Bytes>> handle_open(const Frame& frame);
  [[nodiscard]] Result<std::vector<Bytes>> handle_data(const Frame& frame);
  [[nodiscard]] Result<std::vector<Bytes>> handle_credit(const Frame& frame);
  [[nodiscard]] Result<std::vector<Bytes>> emit(Frame frame);
  void diagnostic(Error error);

  LocalPolicy policy_;
  PluginRegistry registry_;
  FrameCodec codec_;
  ConnectionState state_{ConnectionState::created};
  std::optional<CapabilitySet> capabilities_;
  std::optional<ChannelTable> channels_;
  ReplayWindow replay_;
  std::uint32_t next_frame_seq_{1};
  std::vector<ConnectionEvent> events_;
};

}  // namespace lattice
