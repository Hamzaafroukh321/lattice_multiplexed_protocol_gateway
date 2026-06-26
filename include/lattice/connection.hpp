#pragma once

#include "lattice/channel.hpp"
#include "lattice/executor.hpp"
#include "lattice/plugin.hpp"
#include "lattice/replay.hpp"
#include "lattice/scheduler.hpp"
#include "lattice/transport.hpp"

#include <array>
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

struct ResumeRequest {
  std::array<std::uint8_t, 16> transcript_hash{};
  std::uint16_t epoch{1};
  std::uint32_t first_required_seq{0};
};

struct ConnectionEvent {
  enum class Kind : std::uint8_t {
    negotiated,
    channel_opened,
    message_delivered,
    plugin_response,
    pong_received,
    resumed,
    timer_expired,
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
  ConnectionEngine(LocalPolicy policy, PluginRegistry registry,
                   DeterministicExecutor* executor = nullptr);

  [[nodiscard]] ConnectionState state() const { return state_; }
  [[nodiscard]] const std::optional<CapabilitySet>& capabilities() const { return capabilities_; }
  [[nodiscard]] const std::vector<ConnectionEvent>& events() const { return events_; }

  [[nodiscard]] Result<std::vector<Bytes>> start();
  [[nodiscard]] Result<std::vector<Bytes>> receive(std::span<const std::uint8_t> bytes, bool eof);
  [[nodiscard]] Result<std::pair<ChannelId, std::vector<Bytes>>> open_channel(OpenRequest request);
  [[nodiscard]] Result<std::vector<Bytes>> send(ChannelId id, std::span<const std::uint8_t> payload);
  [[nodiscard]] Result<std::vector<Bytes>> grant_credit(ChannelId id, std::size_t amount);
  [[nodiscard]] Result<std::vector<Bytes>> acknowledge(std::vector<AckRange> ranges);
  [[nodiscard]] Result<std::vector<Bytes>> ping(std::uint64_t token);
  [[nodiscard]] Result<std::vector<Bytes>> resume(ResumeRequest request);
  [[nodiscard]] Result<void> complete_plugin(PluginCompletion completion);
  [[nodiscard]] Result<std::vector<Bytes>> advance_time(std::uint64_t now_ms);
  [[nodiscard]] std::vector<Bytes> flush_outbound(std::size_t writable_bytes);
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
  [[nodiscard]] Result<std::vector<Bytes>> handle_ack(const Frame& frame);
  [[nodiscard]] Result<std::vector<Bytes>> handle_ping(const Frame& frame);
  [[nodiscard]] Result<std::vector<Bytes>> handle_pong(const Frame& frame);
  [[nodiscard]] Result<std::vector<Bytes>> handle_resume(const Frame& frame);
  [[nodiscard]] Result<std::vector<Bytes>> emit(Frame frame);
  [[nodiscard]] Result<void> queue_encoded(Frame frame, Bytes encoded);
  void schedule_timer(TimerKind kind, ChannelId channel, std::uint64_t delay_ms);
  void diagnostic(Error error);

  LocalPolicy policy_;
  PluginRegistry registry_;
  DeterministicExecutor* executor_{nullptr};
  FrameCodec codec_;
  ConnectionState state_{ConnectionState::created};
  std::optional<CapabilitySet> capabilities_;
  std::optional<ChannelTable> channels_;
  ReplayWindow replay_;
  TimerWheel timers_;
  OutboundScheduler scheduler_;
  std::uint32_t next_frame_seq_{1};
  std::uint64_t next_plugin_token_{1};
  std::uint64_t now_ms_{0};
  std::optional<std::uint64_t> pending_pong_token_;
  std::vector<ConnectionEvent> events_;
};

[[nodiscard]] Result<Bytes> encode_resume_payload(const ResumeRequest& request);
[[nodiscard]] Result<ResumeRequest> decode_resume_payload(std::span<const std::uint8_t> payload);
[[nodiscard]] Bytes encode_u64_be(std::uint64_t value);
[[nodiscard]] Result<std::uint64_t> decode_u64_be(std::span<const std::uint8_t> payload);

}  // namespace lattice
