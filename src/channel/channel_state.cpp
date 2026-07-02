#include "lattice/channel.hpp"

#include <algorithm>
#include <limits>

namespace lattice {
namespace {

[[nodiscard]] Error channel_error(ErrorCode code, ChannelId id, std::string detail) {
  Error error = make_error(ErrorScope::channel, code, CloseAction::reset_channel, std::move(detail));
  error.channel_no = id.number;
  error.generation = id.generation;
  return error;
}

}  // namespace

FlowAccount::FlowAccount(std::size_t window) : window_(window), available_(window) {}

Result<void> FlowAccount::reserve(std::size_t amount) {
  if (amount == 0U) {
    return {};
  }
  if (amount > available_) {
    return make_error(ErrorScope::channel, ErrorCode::would_block, CloseAction::none,
                      "insufficient credit");
  }
  available_ -= amount;
  reserved_ += amount;
  return {};
}

Result<void> FlowAccount::release(std::size_t amount) {
  if (amount > reserved_) {
    return make_error(ErrorScope::channel, ErrorCode::flow_underflow, CloseAction::reset_channel,
                      "release exceeds reserved credit");
  }
  reserved_ -= amount;
  std::size_t next = 0;
  if (!checked_add(available_, amount, &next) || next > window_) {
    return make_error(ErrorScope::channel, ErrorCode::flow_overflow,
                      CloseAction::close_connection, "credit release exceeds window");
  }
  available_ = next;
  return {};
}

Result<void> FlowAccount::grant(std::size_t amount) {
  if (amount == 0U) {
    return make_error(ErrorScope::channel, ErrorCode::flow_underflow, CloseAction::reset_channel,
                      "zero CREDIT is illegal");
  }
  std::size_t next = 0;
  if (!checked_add(available_, amount, &next) || next + reserved_ > window_) {
    return make_error(ErrorScope::channel, ErrorCode::flow_overflow,
                      CloseAction::close_connection, "CREDIT exceeds negotiated window");
  }
  available_ = next;
  return {};
}

Reassembler::Reassembler(std::size_t max_message) : max_message_(max_message) {}

Result<std::optional<LogicalMessage>> Reassembler::insert(ChannelId channel,
                                                          std::uint32_t sequence,
                                                          std::uint32_t offset,
                                                          std::uint32_t total,
                                                          std::span<const std::uint8_t> bytes) {
  if (total == 0U || total > max_message_) {
    return channel_error(ErrorCode::resource_limit, channel, "message total exceeds limit");
  }
  std::size_t end = 0;
  if (!checked_add(offset, bytes.size(), &end) || end > total) {
    return channel_error(ErrorCode::fragment_range, channel, "fragment extends past total length");
  }
  auto& pending = pending_[sequence];
  const bool new_pending = pending.total == 0U;
  if (pending.total == 0U) {
    pending.total = total;
  } else if (pending.total != total) {
    reset_message(sequence);
    return channel_error(ErrorCode::fragment_range, channel, "message total changed");
  }

  for (const Range& range : pending.ranges) {
    const std::uint32_t range_end =
        static_cast<std::uint32_t>(range.offset + range.bytes.size());
    const std::uint32_t fragment_end = static_cast<std::uint32_t>(end);
    const bool disjoint = fragment_end <= range.offset || offset >= range_end;
    if (disjoint) {
      continue;
    }
    const std::uint32_t overlap_begin = std::max(offset, range.offset);
    const std::uint32_t overlap_end = std::min(fragment_end, range_end);
    for (std::uint32_t pos = overlap_begin; pos < overlap_end; ++pos) {
      const std::uint8_t old_byte = range.bytes[pos - range.offset];
      const std::uint8_t new_byte = bytes[pos - offset];
      if (old_byte != new_byte) {
        reset_message(sequence);
        return channel_error(ErrorCode::fragment_overlap, channel,
                             "conflicting overlapping fragment bytes");
      }
    }
    if (offset >= range.offset && fragment_end <= range_end) {
      return std::optional<LogicalMessage>{};
    }
  }

  std::size_t next_retained = 0;
  if (!checked_add(retained_, bytes.size(), &next_retained) || next_retained > max_message_) {
    if (new_pending && pending.ranges.empty()) {
      pending_.erase(sequence);
    }
    return channel_error(ErrorCode::resource_limit, channel,
                         "retained incomplete fragments exceed receive budget");
  }

  Range added;
  added.offset = offset;
  added.bytes.assign(bytes.begin(), bytes.end());
  retained_ = next_retained;
  pending.ranges.push_back(std::move(added));
  std::sort(pending.ranges.begin(), pending.ranges.end(),
            [](const Range& a, const Range& b) { return a.offset < b.offset; });

  std::uint32_t covered = 0;
  for (const Range& range : pending.ranges) {
    if (range.offset > covered) {
      return std::optional<LogicalMessage>{};
    }
    const auto range_end = static_cast<std::uint32_t>(range.offset + range.bytes.size());
    covered = std::max(covered, range_end);
  }
  if (covered < pending.total) {
    return std::optional<LogicalMessage>{};
  }

  Bytes payload(pending.total);
  for (const Range& range : pending.ranges) {
    std::copy(range.bytes.begin(), range.bytes.end(),
              payload.begin() + static_cast<std::ptrdiff_t>(range.offset));
    retained_ -= range.bytes.size();
  }
  pending_.erase(sequence);
  return std::optional<LogicalMessage>{LogicalMessage{channel, sequence, std::move(payload)}};
}

void Reassembler::reset_message(std::uint32_t sequence) {
  const auto it = pending_.find(sequence);
  if (it == pending_.end()) {
    return;
  }
  for (const Range& range : it->second.ranges) {
    retained_ -= range.bytes.size();
  }
  pending_.erase(it);
}

ChannelTable::ChannelTable(std::uint16_t max_channels, std::size_t channel_window,
                           std::size_t max_message)
    : channel_window_(channel_window), max_message_(max_message) {
  slots_.reserve(max_channels);
  for (std::uint16_t i = 0; i < max_channels; ++i) {
    ChannelSlot slot;
    slot.id = ChannelId{static_cast<std::uint32_t>(i + 1U), 0U};
    slot.flow = FlowAccount(channel_window_);
    slot.reassembler = Reassembler(max_message_);
    slots_.push_back(std::move(slot));
  }
}

Result<ChannelId> ChannelTable::allocate() {
  for (ChannelSlot& slot : slots_) {
    if (slot.state == ChannelState::free) {
      if (slot.id.generation == std::numeric_limits<std::uint8_t>::max()) {
        return channel_error(ErrorCode::resource_limit, slot.id, "channel generation wrapped");
      }
      slot.id.generation = static_cast<std::uint8_t>(slot.id.generation + 1U);
      slot.state = ChannelState::opening;
      slot.next_send_seq = 1U;
      slot.next_recv_seq = 1U;
      slot.local_half_closed = false;
      slot.remote_half_closed = false;
      slot.flow = FlowAccount(channel_window_);
      slot.reassembler = Reassembler(max_message_);
      return slot.id;
    }
  }
  return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                    "no free channel slots");
}

Result<void> ChannelTable::accept_remote(ChannelId id) {
  if (id.number == 0U || id.number > slots_.size()) {
    return channel_error(ErrorCode::resource_limit, id, "remote channel number is out of range");
  }
  ChannelSlot& slot = slots_[id.number - 1U];
  if (slot.state != ChannelState::free) {
    return channel_error(ErrorCode::illegal_state, id, "remote OPEN targets busy slot");
  }
  const std::uint8_t expected = static_cast<std::uint8_t>(slot.id.generation + 1U);
  if (id.generation != expected) {
    return channel_error(ErrorCode::stale_generation, id, "remote OPEN generation is not next");
  }
  slot.id = id;
  slot.state = ChannelState::open;
  slot.next_send_seq = 1U;
  slot.next_recv_seq = 1U;
  slot.local_half_closed = false;
  slot.remote_half_closed = false;
  slot.flow = FlowAccount(channel_window_);
  slot.reassembler = Reassembler(max_message_);
  return {};
}

Result<void> ChannelTable::activate(ChannelId id) {
  ChannelSlot* slot = find(id);
  if (slot == nullptr || slot->state != ChannelState::opening) {
    return channel_error(ErrorCode::illegal_state, id, "channel cannot activate");
  }
  slot->state = ChannelState::open;
  return {};
}

Result<void> ChannelTable::half_close(ChannelId id, Direction direction) {
  ChannelSlot* slot = find(id);
  if (slot == nullptr) {
    return channel_error(ErrorCode::stale_generation, id, "channel not live");
  }
  if (slot->state != ChannelState::open && slot->state != ChannelState::local_closed &&
      slot->state != ChannelState::remote_closed) {
    return channel_error(ErrorCode::illegal_state, id, "channel half-close is illegal now");
  }
  if (direction == Direction::local_send) {
    slot->local_half_closed = true;
    slot->state = slot->remote_half_closed ? ChannelState::closing : ChannelState::local_closed;
  } else {
    slot->remote_half_closed = true;
    slot->state = slot->local_half_closed ? ChannelState::closing : ChannelState::remote_closed;
  }
  return {};
}

Result<void> ChannelTable::reset(ChannelId id) {
  ChannelSlot* slot = find(id);
  if (slot == nullptr) {
    return channel_error(ErrorCode::stale_generation, id, "reset targets stale channel");
  }
  slot->state = ChannelState::tombstone;
  slot->flow = FlowAccount(channel_window_);
  slot->reassembler = Reassembler(max_message_);
  return {};
}

Result<void> ChannelTable::retire_tombstone(ChannelId id) {
  if (id.number == 0U || id.number > slots_.size()) {
    return channel_error(ErrorCode::illegal_state, id, "only tombstone can retire");
  }
  ChannelSlot* slot = &slots_[id.number - 1U];
  if (slot->id.generation != id.generation || slot->state != ChannelState::tombstone) {
    return channel_error(ErrorCode::illegal_state, id, "only tombstone can retire");
  }
  slot->state = ChannelState::free;
  return {};
}

ChannelSlot* ChannelTable::find(ChannelId id) {
  if (id.number == 0U || id.number > slots_.size()) {
    return nullptr;
  }
  ChannelSlot& slot = slots_[id.number - 1U];
  if (slot.id.generation != id.generation || slot.state == ChannelState::free ||
      slot.state == ChannelState::tombstone) {
    return nullptr;
  }
  return &slot;
}

const ChannelSlot* ChannelTable::find(ChannelId id) const {
  if (id.number == 0U || id.number > slots_.size()) {
    return nullptr;
  }
  const ChannelSlot& slot = slots_[id.number - 1U];
  if (slot.id.generation != id.generation || slot.state == ChannelState::free ||
      slot.state == ChannelState::tombstone) {
    return nullptr;
  }
  return &slot;
}

std::size_t ChannelTable::live_count() const {
  return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const ChannelSlot& slot) {
    return slot.state != ChannelState::free && slot.state != ChannelState::tombstone;
  }));
}

}  // namespace lattice
