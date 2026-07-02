#pragma once

#include "lattice/error.hpp"
#include "lattice/frame.hpp"
#include "lattice/types.hpp"

#include <map>
#include <optional>

namespace lattice {

class FlowAccount {
 public:
  explicit FlowAccount(std::size_t window = 0);

  [[nodiscard]] std::size_t window() const { return window_; }
  [[nodiscard]] std::size_t available() const { return available_; }
  [[nodiscard]] std::size_t reserved() const { return reserved_; }

  [[nodiscard]] Result<void> reserve(std::size_t amount);
  [[nodiscard]] Result<void> release(std::size_t amount);
  [[nodiscard]] Result<void> grant(std::size_t amount);
  [[nodiscard]] bool invariant_holds() const { return available_ + reserved_ <= window_; }

 private:
  std::size_t window_{0};
  std::size_t available_{0};
  std::size_t reserved_{0};
};

struct LogicalMessage {
  ChannelId channel;
  std::uint32_t sequence{0};
  Bytes payload;
};

class Reassembler {
 public:
  explicit Reassembler(std::size_t max_message = kDefaultMaxMessage);

  [[nodiscard]] Result<std::optional<LogicalMessage>> insert(ChannelId channel,
                                                             std::uint32_t sequence,
                                                             std::uint32_t offset,
                                                             std::uint32_t total,
                                                             std::span<const std::uint8_t> bytes);
  void reset_message(std::uint32_t sequence);
  [[nodiscard]] std::size_t retained_bytes() const { return retained_; }

 private:
  struct Range {
    std::uint32_t offset{0};
    Bytes bytes;
  };
  struct Pending {
    std::uint32_t total{0};
    std::vector<Range> ranges;
  };

  std::size_t max_message_;
  std::map<std::uint32_t, Pending> pending_;
  std::size_t retained_{0};
};

struct ChannelSlot {
  ChannelId id;
  ChannelState state{ChannelState::free};
  std::uint32_t next_send_seq{1};
  std::uint32_t next_recv_seq{1};
  bool local_half_closed{false};
  bool remote_half_closed{false};
  FlowAccount flow;
  Reassembler reassembler;
};

class ChannelTable {
 public:
  ChannelTable(std::uint16_t max_channels, std::size_t channel_window,
               std::size_t max_message);

  [[nodiscard]] Result<ChannelId> allocate();
  [[nodiscard]] Result<void> accept_remote(ChannelId id);
  [[nodiscard]] Result<void> activate(ChannelId id);
  [[nodiscard]] Result<std::uint32_t> reserve_send_sequence(ChannelId id);
  [[nodiscard]] Result<void> half_close(ChannelId id, Direction direction);
  [[nodiscard]] Result<void> reset(ChannelId id);
  [[nodiscard]] Result<void> retire_tombstone(ChannelId id);
  [[nodiscard]] ChannelSlot* find(ChannelId id);
  [[nodiscard]] const ChannelSlot* find(ChannelId id) const;
  [[nodiscard]] std::size_t live_count() const;

 private:
  std::vector<ChannelSlot> slots_;
  std::size_t channel_window_;
  std::size_t max_message_;
};

}  // namespace lattice
