#include "lattice/connection.hpp"

#include <algorithm>
#include <array>

namespace lattice {
namespace {

void append_u16(Bytes& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u64(Bytes& out, std::uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xFFU));
  }
}

[[nodiscard]] Result<std::uint16_t> read_u16(std::span<const std::uint8_t> bytes,
                                             std::size_t* cursor) {
  if (*cursor > bytes.size() || bytes.size() - *cursor < 2U) {
    return make_error(ErrorScope::connection, ErrorCode::negotiation_rejected,
                      CloseAction::close_connection, "short HELLO u16");
  }
  const std::uint16_t value =
      static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[*cursor]) << 8U) |
                                 static_cast<std::uint16_t>(bytes[*cursor + 1U]));
  *cursor += 2U;
  return value;
}

[[nodiscard]] Result<std::uint32_t> read_u32(std::span<const std::uint8_t> bytes,
                                             std::size_t* cursor) {
  auto value = read_u32_be(bytes, *cursor);
  if (!value) {
    return make_error(ErrorScope::connection, ErrorCode::negotiation_rejected,
                      CloseAction::close_connection, "short HELLO u32");
  }
  *cursor += 4U;
  return value.value();
}

[[nodiscard]] Result<std::uint64_t> read_u64(std::span<const std::uint8_t> bytes,
                                             std::size_t* cursor) {
  if (*cursor > bytes.size() || bytes.size() - *cursor < 8U) {
    return make_error(ErrorScope::connection, ErrorCode::negotiation_rejected,
                      CloseAction::close_connection, "short HELLO u64");
  }
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8U; ++i) {
    value = (value << 8U) | bytes[*cursor + i];
  }
  *cursor += 8U;
  return value;
}

[[nodiscard]] std::array<std::uint8_t, 16> transcript_hash(const CapabilitySet& caps) {
  Bytes encoded;
  append_u16(encoded, caps.major);
  append_u32(encoded, static_cast<std::uint32_t>(caps.max_frame));
  append_u32(encoded, static_cast<std::uint32_t>(caps.max_message));
  append_u32(encoded, static_cast<std::uint32_t>(caps.connection_window));
  append_u16(encoded, caps.max_channels);
  append_u64(encoded, caps.features);
  for (const auto& plugin : caps.plugins) {
    append_u32(encoded, plugin.family_id);
    append_u64(encoded, plugin.schema_hash);
    append_u32(encoded, plugin.max_depth);
  }
  std::array<std::uint8_t, 16> out{};
  std::uint32_t first = crc32c(encoded);
  encoded.push_back(0xA5U);
  std::uint32_t second = crc32c(encoded);
  for (std::size_t i = 0; i < 4U; ++i) {
    out[i] = static_cast<std::uint8_t>((first >> (24U - 8U * i)) & 0xFFU);
    out[i + 4U] = static_cast<std::uint8_t>((second >> (24U - 8U * i)) & 0xFFU);
    out[i + 8U] = static_cast<std::uint8_t>((first ^ second) >> (24U - 8U * i));
    out[i + 12U] = static_cast<std::uint8_t>((first + second) >> (24U - 8U * i));
  }
  return out;
}

[[nodiscard]] Error negotiation_error(std::string detail) {
  return make_error(ErrorScope::connection, ErrorCode::negotiation_rejected,
                    CloseAction::close_connection, std::move(detail));
}

}  // namespace

Result<Bytes> encode_hello_payload(const HelloMessage& hello) {
  if (hello.min_major > hello.max_major) {
    return negotiation_error("invalid version range");
  }
  Bytes out;
  append_u16(out, hello.min_major);
  append_u16(out, hello.max_major);
  append_u32(out, static_cast<std::uint32_t>(hello.limits.max_frame));
  append_u32(out, static_cast<std::uint32_t>(hello.limits.max_message));
  append_u32(out, static_cast<std::uint32_t>(hello.limits.connection_window));
  append_u16(out, hello.limits.max_channels);
  append_u64(out, hello.limits.required_features);
  append_u64(out, hello.limits.optional_features);
  append_u16(out, static_cast<std::uint16_t>(hello.plugins.size()));
  std::vector<PluginDescriptor> plugins = hello.plugins;
  std::sort(plugins.begin(), plugins.end(), [](const PluginDescriptor& a, const PluginDescriptor& b) {
    return a.family_id < b.family_id;
  });
  std::optional<std::uint32_t> previous;
  for (const PluginDescriptor& plugin : plugins) {
    if (previous.has_value() && previous.value() == plugin.family_id) {
      return make_error(ErrorScope::plugin, ErrorCode::duplicate_required_tlv,
                        CloseAction::close_connection, "duplicate plugin family in HELLO");
    }
    previous = plugin.family_id;
    append_u32(out, plugin.family_id);
    append_u64(out, plugin.schema_hash);
    append_u32(out, plugin.max_depth);
  }
  return out;
}

Result<HelloMessage> decode_hello_payload(std::span<const std::uint8_t> payload) {
  std::size_t cursor = 0;
  auto min_major = read_u16(payload, &cursor);
  auto max_major = read_u16(payload, &cursor);
  auto max_frame = read_u32(payload, &cursor);
  auto max_message = read_u32(payload, &cursor);
  auto conn_window = read_u32(payload, &cursor);
  auto max_channels = read_u16(payload, &cursor);
  auto required = read_u64(payload, &cursor);
  auto optional = read_u64(payload, &cursor);
  auto plugin_count = read_u16(payload, &cursor);
  if (!min_major || !max_major || !max_frame || !max_message || !conn_window ||
      !max_channels || !required || !optional || !plugin_count) {
    return negotiation_error("malformed HELLO payload");
  }
  HelloMessage hello;
  hello.min_major = min_major.value();
  hello.max_major = max_major.value();
  hello.limits.min_major = hello.min_major;
  hello.limits.max_major = hello.max_major;
  hello.limits.max_frame = max_frame.value();
  hello.limits.max_message = max_message.value();
  hello.limits.connection_window = conn_window.value();
  hello.limits.max_channels = max_channels.value();
  hello.limits.required_features = required.value();
  hello.limits.optional_features = optional.value();
  for (std::uint16_t i = 0; i < plugin_count.value(); ++i) {
    auto family = read_u32(payload, &cursor);
    auto schema = read_u64(payload, &cursor);
    auto depth = read_u32(payload, &cursor);
    if (!family || !schema || !depth) {
      return negotiation_error("truncated plugin descriptor in HELLO");
    }
    hello.plugins.push_back(PluginDescriptor{family.value(), schema.value(), depth.value()});
  }
  if (cursor != payload.size()) {
    return negotiation_error("HELLO payload has trailing bytes");
  }
  return hello;
}

Result<CapabilitySet> negotiate(const HelloMessage& local, const HelloMessage& peer) {
  if (local.max_major < peer.min_major || peer.max_major < local.min_major) {
    return negotiation_error("no compatible major version");
  }
  CapabilitySet caps;
  caps.major = std::min(local.max_major, peer.max_major);
  caps.max_frame = std::min(local.limits.max_frame, peer.limits.max_frame);
  caps.max_message = std::min(local.limits.max_message, peer.limits.max_message);
  caps.connection_window = std::min(local.limits.connection_window, peer.limits.connection_window);
  caps.max_channels = std::min(local.limits.max_channels, peer.limits.max_channels);
  if (caps.max_frame < 128U || caps.max_message < 1U || caps.connection_window < caps.max_frame ||
      caps.max_channels == 0U) {
    return negotiation_error("negotiated limits below minimum");
  }
  const std::uint64_t common_features =
      (local.limits.required_features | local.limits.optional_features) &
      (peer.limits.required_features | peer.limits.optional_features);
  if ((local.limits.required_features & ~common_features) != 0U ||
      (peer.limits.required_features & ~common_features) != 0U) {
    return make_error(ErrorScope::connection, ErrorCode::unknown_required_feature,
                      CloseAction::close_connection, "required feature is unsupported");
  }
  caps.features = common_features;
  for (const PluginDescriptor& lhs : local.plugins) {
    for (const PluginDescriptor& rhs : peer.plugins) {
      if (lhs.family_id == rhs.family_id) {
        if (lhs.schema_hash != rhs.schema_hash) {
          return make_error(ErrorScope::plugin, ErrorCode::negotiation_rejected,
                            CloseAction::close_connection, "plugin schema hash mismatch");
        }
        caps.plugins.push_back(PluginDescriptor{
            lhs.family_id, lhs.schema_hash, std::min(lhs.max_depth, rhs.max_depth)});
      }
    }
  }
  if (caps.plugins.empty()) {
    return make_error(ErrorScope::plugin, ErrorCode::negotiation_rejected,
                      CloseAction::close_connection, "no mutually supported plugin family");
  }
  std::sort(caps.plugins.begin(), caps.plugins.end(), [](const PluginDescriptor& a, const PluginDescriptor& b) {
    return a.family_id < b.family_id;
  });
  caps.transcript_hash = transcript_hash(caps);
  return caps;
}

}  // namespace lattice
