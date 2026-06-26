#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace lattice {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kLtxMagic = 0x4C545831U;
constexpr std::uint32_t kMaxChannelNo = 0x00FFFFFFU;
constexpr std::size_t kDefaultMaxFrame = 64U * 1024U;
constexpr std::size_t kDefaultMaxMessage = 1024U * 1024U;
constexpr std::size_t kDefaultConnectionWindow = 1024U * 1024U;
constexpr std::uint16_t kDefaultMaxChannels = 256U;
constexpr std::size_t kMaxVarintBytes = 5U;
constexpr std::size_t kMaxExtensionDepth = 8U;

enum class FrameType : std::uint8_t {
  hello = 0x01,
  open = 0x10,
  data = 0x11,
  ack = 0x12,
  credit = 0x13,
  half_close = 0x14,
  reset = 0x15,
  ping = 0x20,
  pong = 0x21,
  goaway = 0x22,
  resume = 0x23
};

enum class ConnectionState : std::uint8_t {
  created,
  negotiating,
  active,
  draining,
  closed
};

enum class ChannelState : std::uint8_t {
  free,
  opening,
  open,
  local_closed,
  remote_closed,
  closing,
  tombstone
};

enum class Direction : std::uint8_t {
  local_send,
  remote_send
};

struct ChannelId {
  std::uint32_t number{0};
  std::uint8_t generation{0};

  [[nodiscard]] bool is_control() const { return number == 0 && generation == 0; }
  [[nodiscard]] std::string str() const;
  friend bool operator==(const ChannelId&, const ChannelId&) = default;
};

struct LocalPolicy {
  std::uint16_t min_major{1};
  std::uint16_t max_major{1};
  std::size_t max_frame{kDefaultMaxFrame};
  std::size_t max_message{kDefaultMaxMessage};
  std::size_t connection_window{kDefaultConnectionWindow};
  std::uint16_t max_channels{kDefaultMaxChannels};
  std::uint64_t required_features{0};
  std::uint64_t optional_features{0};
  std::uint32_t replay_window{4096};
};

struct PluginDescriptor {
  std::uint32_t family_id{0};
  std::uint64_t schema_hash{0};
  std::uint32_t max_depth{8};

  friend bool operator==(const PluginDescriptor&, const PluginDescriptor&) = default;
};

struct CapabilitySet {
  std::uint16_t major{1};
  std::size_t max_frame{kDefaultMaxFrame};
  std::size_t max_message{kDefaultMaxMessage};
  std::size_t connection_window{kDefaultConnectionWindow};
  std::uint16_t max_channels{kDefaultMaxChannels};
  std::uint64_t features{0};
  std::array<std::uint8_t, 16> transcript_hash{};
  std::vector<PluginDescriptor> plugins;
};

[[nodiscard]] bool checked_add(std::size_t a, std::size_t b, std::size_t* out);
[[nodiscard]] bool checked_mul(std::size_t a, std::size_t b, std::size_t* out);

}  // namespace lattice
