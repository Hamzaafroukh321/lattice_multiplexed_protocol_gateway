#include "lattice/frame.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

#if defined(__x86_64__) || defined(_M_X64)
#if defined(_WIN32)
#include <intrin.h>
#endif
#include <nmmintrin.h>
#endif

namespace lattice {
namespace {

constexpr std::size_t kFixedPrefixBytes = 16U;
constexpr std::uint16_t kRequiredBit = 0x8000U;

[[nodiscard]] bool known_required_extension(std::uint16_t type) {
  switch (type) {
    case 1U:
    case 2U:
    case 3U:
    case 4U:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] const char* scope_name(ErrorScope scope) {
  switch (scope) {
    case ErrorScope::connection: return "connection";
    case ErrorScope::channel: return "channel";
    case ErrorScope::message: return "message";
    case ErrorScope::plugin: return "plugin";
    case ErrorScope::transport: return "transport";
    case ErrorScope::internal: return "internal";
  }
  return "internal";
}

[[nodiscard]] const char* code_name(ErrorCode code) {
  switch (code) {
    case ErrorCode::need_more_data: return "NeedMoreData";
    case ErrorCode::truncated_frame: return "TruncatedFrame";
    case ErrorCode::bad_magic: return "BadMagic";
    case ErrorCode::reserved_flags: return "ReservedFlags";
    case ErrorCode::varint_non_canonical: return "VarintNonCanonical";
    case ErrorCode::varint_too_long: return "VarintTooLong";
    case ErrorCode::frame_too_large: return "FrameTooLarge";
    case ErrorCode::header_too_large: return "HeaderTooLarge";
    case ErrorCode::crc_mismatch: return "CrcMismatch";
    case ErrorCode::malformed_tlv: return "MalformedTlv";
    case ErrorCode::unsupported_frame_type: return "UnsupportedFrameType";
    case ErrorCode::negotiation_rejected: return "NegotiationRejected";
    case ErrorCode::duplicate_required_tlv: return "DuplicateRequiredTlv";
    case ErrorCode::unknown_required_feature: return "UnknownRequiredFeature";
    case ErrorCode::stale_generation: return "StaleGeneration";
    case ErrorCode::illegal_state: return "IllegalState";
    case ErrorCode::fragment_range: return "FragmentRange";
    case ErrorCode::fragment_overlap: return "FragmentOverlap";
    case ErrorCode::sequence_error: return "SequenceError";
    case ErrorCode::flow_underflow: return "FlowUnderflow";
    case ErrorCode::flow_overflow: return "FlowOverflow";
    case ErrorCode::resource_limit: return "ResourceLimit";
    case ErrorCode::would_block: return "WouldBlock";
    case ErrorCode::plugin_decode: return "PluginDecode";
    case ErrorCode::resume_rejected: return "ResumeRejected";
    case ErrorCode::timeout: return "Timeout";
    case ErrorCode::cancelled: return "Cancelled";
    case ErrorCode::transport_error: return "Transport";
    case ErrorCode::invariant_failure: return "InvariantFailure";
  }
  return "InvariantFailure";
}

void append_u16(Bytes& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u24(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

[[nodiscard]] bool known_frame_type(std::uint8_t raw) {
  switch (static_cast<FrameType>(raw)) {
    case FrameType::hello:
    case FrameType::open:
    case FrameType::data:
    case FrameType::ack:
    case FrameType::credit:
    case FrameType::half_close:
    case FrameType::reset:
    case FrameType::ping:
    case FrameType::pong:
    case FrameType::goaway:
    case FrameType::resume:
      return true;
  }
  return false;
}

[[nodiscard]] DecodeEvent error_event(Error error) {
  return DecodeEvent{DecodeStatus::error, std::nullopt, std::move(error)};
}

[[nodiscard]] bool cpu_supports_sse42() {
#if (defined(__x86_64__) || defined(_M_X64)) && defined(_WIN32)
  int registers[4] = {};
  __cpuid(registers, 1);
  return (registers[2] & (1 << 20)) != 0;
#elif (defined(__x86_64__) || defined(_M_X64)) && (defined(__clang__) || defined(__GNUC__))
  return __builtin_cpu_supports("sse4.2");
#else
  return false;
#endif
}

#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__clang__) || defined(__GNUC__))
[[nodiscard]] __attribute__((target("sse4.2"))) std::uint32_t crc32c_sse42(
    std::span<const std::uint8_t> bytes) {
  std::uint64_t crc = 0xFFFFFFFFU;
  std::size_t cursor = 0;
  while (bytes.size() - cursor >= 8U) {
    std::uint64_t block = 0;
    std::memcpy(&block, bytes.data() + cursor, sizeof(block));
    crc = _mm_crc32_u64(crc, block);
    cursor += 8U;
  }
  std::uint32_t crc32 = static_cast<std::uint32_t>(crc);
  while (cursor < bytes.size()) {
    crc32 = _mm_crc32_u8(crc32, bytes[cursor]);
    ++cursor;
  }
  return ~crc32;
}
#endif

}  // namespace

std::string Error::stable_code() const {
  std::ostringstream out;
  out << scope_name(scope) << "." << code_name(code);
  return out.str();
}

Error make_error(ErrorScope scope, ErrorCode code, CloseAction action, std::string detail) {
  Error error;
  error.scope = scope;
  error.code = code;
  error.action = action;
  error.detail = std::move(detail);
  return error;
}

std::string ChannelId::str() const {
  std::ostringstream out;
  out << number << ":" << static_cast<unsigned>(generation);
  return out.str();
}

bool checked_add(std::size_t a, std::size_t b, std::size_t* out) {
  if (out == nullptr) {
    return false;
  }
  if (a > std::numeric_limits<std::size_t>::max() - b) {
    return false;
  }
  *out = a + b;
  return true;
}

bool checked_mul(std::size_t a, std::size_t b, std::size_t* out) {
  if (out == nullptr) {
    return false;
  }
  if (a != 0U && b > std::numeric_limits<std::size_t>::max() / a) {
    return false;
  }
  *out = a * b;
  return true;
}

Result<std::uint32_t> read_u32_be(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4U) {
    return make_error(ErrorScope::connection, ErrorCode::truncated_frame,
                      CloseAction::close_connection, "not enough bytes for u32");
  }
  const std::uint32_t value =
      (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
      (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
      (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
      static_cast<std::uint32_t>(bytes[offset + 3U]);
  return value;
}

std::uint32_t crc32c(std::span<const std::uint8_t> bytes) {
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__clang__) || defined(__GNUC__))
  static const bool use_sse42 = cpu_supports_sse42();
  if (use_sse42) {
    return crc32c_sse42(bytes);
  }
#endif
  static const std::array<std::array<std::uint32_t, 256>, 8> table = [] {
    std::array<std::array<std::uint32_t, 256>, 8> out{};
    for (std::uint32_t i = 0; i < out[0].size(); ++i) {
      std::uint32_t crc = i;
      for (int bit = 0; bit < 8; ++bit) {
        const std::uint32_t mask = static_cast<std::uint32_t>(-(static_cast<int>(crc & 1U)));
        crc = (crc >> 1U) ^ (0x82F63B78U & mask);
      }
      out[0][i] = crc;
    }
    for (std::size_t slice = 1; slice < out.size(); ++slice) {
      for (std::uint32_t i = 0; i < out[slice].size(); ++i) {
        const std::uint32_t previous = out[slice - 1U][i];
        out[slice][i] = out[0][previous & 0xFFU] ^ (previous >> 8U);
      }
    }
    return out;
  }();
  std::uint32_t crc = 0xFFFFFFFFU;
  std::size_t cursor = 0;
  while (bytes.size() - cursor >= 8U) {
    std::uint64_t block = 0;
    std::memcpy(&block, bytes.data() + cursor, sizeof(block));
    crc ^= static_cast<std::uint32_t>(block & 0xFFFFFFFFULL);
    const std::uint32_t high = static_cast<std::uint32_t>(block >> 32U);
    crc = table[7][crc & 0xFFU] ^
          table[6][(crc >> 8U) & 0xFFU] ^
          table[5][(crc >> 16U) & 0xFFU] ^
          table[4][(crc >> 24U) & 0xFFU] ^
          table[3][high & 0xFFU] ^
          table[2][(high >> 8U) & 0xFFU] ^
          table[1][(high >> 16U) & 0xFFU] ^
          table[0][(high >> 24U) & 0xFFU];
    cursor += 8U;
  }
  while (cursor < bytes.size()) {
    crc = table[0][(crc ^ bytes[cursor]) & 0xFFU] ^ (crc >> 8U);
    ++cursor;
  }
  return ~crc;
}

Result<Bytes> encode_uleb128(std::uint32_t value) {
  Bytes out;
  do {
    std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7FU);
    value >>= 7U;
    if (value != 0U) {
      byte = static_cast<std::uint8_t>(byte | 0x80U);
    }
    out.push_back(byte);
  } while (value != 0U);
  if (out.size() > kMaxVarintBytes) {
    return make_error(ErrorScope::connection, ErrorCode::varint_too_long,
                      CloseAction::close_connection, "varint exceeds protocol cap");
  }
  return out;
}

Result<std::pair<std::uint32_t, std::size_t>> decode_uleb128(
    std::span<const std::uint8_t> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  std::uint32_t shift = 0;
  for (std::size_t i = 0; i < kMaxVarintBytes; ++i) {
    if (offset + i >= bytes.size()) {
      return make_error(ErrorScope::connection, ErrorCode::need_more_data, CloseAction::none,
                        "partial varint");
    }
    const std::uint8_t byte = bytes[offset + i];
    value |= static_cast<std::uint32_t>(byte & 0x7FU) << shift;
    if ((byte & 0x80U) == 0U) {
      if ((i == 4U && (byte & 0xF0U) != 0U) ||
          (i > 0U && value < (std::uint32_t{1} << (7U * static_cast<std::uint32_t>(i))))) {
        return make_error(ErrorScope::connection, ErrorCode::varint_non_canonical,
                          CloseAction::close_connection, "non-minimal ULEB128");
      }
      return std::make_pair(value, i + 1U);
    }
    shift += 7U;
  }
  return make_error(ErrorScope::connection, ErrorCode::varint_too_long,
                    CloseAction::close_connection, "ULEB128 longer than five bytes");
}

Result<std::vector<Extension>> parse_extensions(std::span<const std::uint8_t> bytes) {
  std::vector<Extension> extensions;
  std::size_t cursor = 0;
  std::optional<std::uint16_t> previous;
  while (cursor < bytes.size()) {
    if (bytes.size() - cursor < 2U) {
      return make_error(ErrorScope::connection, ErrorCode::malformed_tlv,
                        CloseAction::close_connection, "short extension type");
    }
    const std::uint16_t raw =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[cursor]) << 8U) |
                                   static_cast<std::uint16_t>(bytes[cursor + 1U]));
    cursor += 2U;
    auto length = decode_uleb128(bytes, cursor);
    if (!length) {
      Error error = length.error();
      error.code = error.code == ErrorCode::need_more_data ? ErrorCode::malformed_tlv : error.code;
      error.action = CloseAction::close_connection;
      return error;
    }
    cursor += length.value().second;
    const std::uint32_t len = length.value().first;
    if (len > bytes.size() - cursor) {
      return make_error(ErrorScope::connection, ErrorCode::malformed_tlv,
                        CloseAction::close_connection, "extension length exceeds header");
    }
    const std::uint16_t type = static_cast<std::uint16_t>(raw & ~kRequiredBit);
    if (previous.has_value() && type < previous.value()) {
      return make_error(ErrorScope::connection, ErrorCode::malformed_tlv,
                        CloseAction::close_connection, "extensions are not sorted");
    }
    previous = type;
    Extension extension;
    extension.type = type;
    extension.required = (raw & kRequiredBit) != 0U;
    if (extension.required && !known_required_extension(extension.type)) {
      return make_error(ErrorScope::connection, ErrorCode::unknown_required_feature,
                        CloseAction::close_connection, "unknown required frame extension");
    }
    extension.value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                           bytes.begin() + static_cast<std::ptrdiff_t>(cursor + len));
    extensions.push_back(std::move(extension));
    cursor += len;
  }
  return extensions;
}

Result<Bytes> write_extensions(std::vector<Extension> extensions) {
  std::stable_sort(extensions.begin(), extensions.end(), [](const Extension& a, const Extension& b) {
    if (a.type == b.type) {
      return a.required && !b.required;
    }
    return a.type < b.type;
  });
  Bytes out;
  for (const Extension& extension : extensions) {
    if (extension.type > 0x7FFFU) {
      return make_error(ErrorScope::connection, ErrorCode::malformed_tlv,
                        CloseAction::close_connection, "extension type exceeds canonical range");
    }
    const std::uint16_t raw =
        static_cast<std::uint16_t>(extension.type | (extension.required ? kRequiredBit : 0U));
    append_u16(out, raw);
    auto encoded_len = encode_uleb128(static_cast<std::uint32_t>(extension.value.size()));
    if (!encoded_len) {
      return encoded_len.error();
    }
    out.insert(out.end(), encoded_len.value().begin(), encoded_len.value().end());
    out.insert(out.end(), extension.value.begin(), extension.value.end());
  }
  return out;
}

std::optional<Extension> find_extension(const Frame& frame, std::uint16_t type) {
  const auto it = std::find_if(frame.extensions.begin(), frame.extensions.end(),
                               [type](const Extension& ext) { return ext.type == type; });
  if (it == frame.extensions.end()) {
    return std::nullopt;
  }
  return *it;
}

Result<std::uint32_t> extension_u32(const Frame& frame, std::uint16_t type) {
  auto ext = find_extension(frame, type);
  if (!ext || ext->value.size() != 4U) {
    return make_error(ErrorScope::message, ErrorCode::malformed_tlv,
                      CloseAction::reset_channel, "missing u32 extension");
  }
  return read_u32_be(ext->value, 0);
}

FrameCodec::FrameCodec(FrameLimits limits) : limits_(limits) {}

Result<Bytes> FrameCodec::encode(const Frame& frame) const {
  if (frame.flags != 0U) {
    return make_error(ErrorScope::connection, ErrorCode::reserved_flags,
                      CloseAction::close_connection, "reserved frame flags are nonzero");
  }
  if (frame.channel.number > kMaxChannelNo) {
    return make_error(ErrorScope::channel, ErrorCode::resource_limit,
                      CloseAction::reset_channel, "channel number exceeds 24 bits");
  }
  auto ext = write_extensions(frame.extensions);
  if (!ext) {
    return ext.error();
  }
  if (ext.value().size() > limits_.max_header) {
    return make_error(ErrorScope::connection, ErrorCode::header_too_large,
                      CloseAction::close_connection, "extension header exceeds limit");
  }
  if (frame.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return make_error(ErrorScope::connection, ErrorCode::frame_too_large,
                      CloseAction::close_connection, "payload exceeds protocol varint range");
  }
  auto payload_len = encode_uleb128(static_cast<std::uint32_t>(frame.payload.size()));
  if (!payload_len) {
    return payload_len.error();
  }
  Bytes out;
  append_u32(out, kLtxMagic);
  out.push_back(static_cast<std::uint8_t>(frame.type));
  out.push_back(frame.flags);
  append_u16(out, static_cast<std::uint16_t>(ext.value().size()));
  append_u24(out, frame.channel.number);
  out.push_back(frame.channel.generation);
  append_u32(out, frame.frame_seq);
  out.insert(out.end(), payload_len.value().begin(), payload_len.value().end());
  out.insert(out.end(), ext.value().begin(), ext.value().end());
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  std::size_t total_without_crc = out.size();
  std::size_t total = 0;
  if (!checked_add(total_without_crc, 4U, &total) || total > limits_.max_frame) {
    return make_error(ErrorScope::connection, ErrorCode::frame_too_large,
                      CloseAction::close_connection, "encoded frame exceeds limit");
  }
  append_u32(out, crc32c(out));
  return out;
}

std::vector<DecodeEvent> FrameCodec::feed(std::span<const std::uint8_t> bytes, bool eof) {
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
  std::vector<DecodeEvent> events;
  if (!bytes.empty()) {
    events.reserve(std::max<std::size_t>(1U, bytes.size() / 1024U));
  }
  std::size_t consumed = 0;
  for (;;) {
    const std::size_t remaining = buffer_.size() - consumed;
    if (remaining < kFixedPrefixBytes) {
      if (eof && remaining > 0U) {
        events.push_back(error_event(make_error(ErrorScope::connection,
                                                ErrorCode::truncated_frame,
                                                CloseAction::close_connection,
                                                "EOF before fixed prefix")));
        buffer_.clear();
      } else {
        if (consumed > 0U) {
          buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
        if (events.empty()) {
          events.push_back(DecodeEvent{DecodeStatus::need_more, std::nullopt, std::nullopt});
        }
      }
      return events;
    }

    const std::span<const std::uint8_t> view(buffer_.data() + static_cast<std::ptrdiff_t>(consumed),
                                             remaining);
    const std::uint32_t magic =
        (static_cast<std::uint32_t>(view[0]) << 24U) |
        (static_cast<std::uint32_t>(view[1]) << 16U) |
        (static_cast<std::uint32_t>(view[2]) << 8U) |
        static_cast<std::uint32_t>(view[3]);
    if (magic != kLtxMagic) {
      events.push_back(error_event(make_error(ErrorScope::connection, ErrorCode::bad_magic,
                                              CloseAction::close_connection,
                                              "LTX1 magic mismatch")));
      buffer_.clear();
      return events;
    }
    const std::uint8_t raw_type = view[4];
    if (!known_frame_type(raw_type)) {
      events.push_back(error_event(make_error(ErrorScope::connection,
                                              ErrorCode::unsupported_frame_type,
                                              CloseAction::close_connection,
                                              "unknown frame type")));
      buffer_.clear();
      return events;
    }
    const std::uint8_t flags = view[5];
    if (flags != 0U) {
      events.push_back(error_event(make_error(ErrorScope::connection, ErrorCode::reserved_flags,
                                              CloseAction::close_connection,
                                              "reserved flags are set")));
      buffer_.clear();
      return events;
    }
    const std::uint16_t ext_len =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(view[6]) << 8U) |
                                   static_cast<std::uint16_t>(view[7]));
    if (ext_len > limits_.max_header) {
      events.push_back(error_event(make_error(ErrorScope::connection, ErrorCode::header_too_large,
                                              CloseAction::close_connection,
                                              "extension header exceeds limit")));
      buffer_.clear();
      return events;
    }
    const std::uint32_t channel_no =
        (static_cast<std::uint32_t>(view[8]) << 16U) |
        (static_cast<std::uint32_t>(view[9]) << 8U) |
        static_cast<std::uint32_t>(view[10]);
    const std::uint8_t generation = view[11];
    const std::uint32_t frame_seq =
        (static_cast<std::uint32_t>(view[12]) << 24U) |
        (static_cast<std::uint32_t>(view[13]) << 16U) |
        (static_cast<std::uint32_t>(view[14]) << 8U) |
        static_cast<std::uint32_t>(view[15]);
    const auto payload_len = decode_uleb128(view, 16);
    if (!payload_len) {
      if (payload_len.error().code == ErrorCode::need_more_data && !eof) {
        if (consumed > 0U) {
          buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
        if (events.empty()) {
          events.push_back(DecodeEvent{DecodeStatus::need_more, std::nullopt, std::nullopt});
        }
        return events;
      }
      Error error = payload_len.error();
      if (eof && error.code == ErrorCode::need_more_data) {
        error.code = ErrorCode::truncated_frame;
        error.action = CloseAction::close_connection;
      }
      events.push_back(error_event(error));
      buffer_.clear();
      return events;
    }
    const std::size_t varint_bytes = payload_len.value().second;
    const std::size_t payload_size = payload_len.value().first;
    std::size_t header_total = 0;
    if (!checked_add(16U + varint_bytes, ext_len, &header_total)) {
      events.push_back(error_event(make_error(ErrorScope::connection, ErrorCode::frame_too_large,
                                              CloseAction::close_connection,
                                              "header size overflow")));
      buffer_.clear();
      return events;
    }
    std::size_t without_crc = 0;
    std::size_t total = 0;
    if (!checked_add(header_total, payload_size, &without_crc) ||
        !checked_add(without_crc, 4U, &total) || total > limits_.max_frame) {
      events.push_back(error_event(make_error(ErrorScope::connection, ErrorCode::frame_too_large,
                                              CloseAction::close_connection,
                                              "frame exceeds negotiated limit")));
      buffer_.clear();
      return events;
    }
    if (remaining < total) {
      if (eof) {
        events.push_back(error_event(make_error(ErrorScope::connection, ErrorCode::truncated_frame,
                                                CloseAction::close_connection,
                                                "EOF inside frame")));
        buffer_.clear();
      } else {
        if (consumed > 0U) {
          buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
        if (events.empty()) {
          events.push_back(DecodeEvent{DecodeStatus::need_more, std::nullopt, std::nullopt});
        }
      }
      return events;
    }
    const std::uint32_t expected_crc =
        (static_cast<std::uint32_t>(view[without_crc]) << 24U) |
        (static_cast<std::uint32_t>(view[without_crc + 1U]) << 16U) |
        (static_cast<std::uint32_t>(view[without_crc + 2U]) << 8U) |
        static_cast<std::uint32_t>(view[without_crc + 3U]);
    const std::uint32_t actual_crc = crc32c(view.subspan(0, without_crc));
    if (actual_crc != expected_crc) {
      events.push_back(error_event(make_error(ErrorScope::connection, ErrorCode::crc_mismatch,
                                              CloseAction::close_connection,
                                              "CRC32C mismatch")));
      buffer_.clear();
      return events;
    }
    std::vector<Extension> extensions;
    if (ext_len > 0U) {
      auto parsed_extensions = parse_extensions(view.subspan(16U + varint_bytes, ext_len));
      if (!parsed_extensions) {
        events.push_back(error_event(parsed_extensions.error()));
        buffer_.clear();
        return events;
      }
      extensions = parsed_extensions.take_value();
    }
    Frame frame;
    frame.type = static_cast<FrameType>(raw_type);
    frame.flags = flags;
    frame.channel = ChannelId{channel_no, generation};
    frame.frame_seq = frame_seq;
    frame.extensions = std::move(extensions);
    frame.payload.assign(view.begin() + static_cast<std::ptrdiff_t>(header_total),
                         view.begin() + static_cast<std::ptrdiff_t>(header_total + payload_size));
    events.push_back(DecodeEvent{DecodeStatus::frame, std::move(frame), std::nullopt});
    consumed += total;
    if (consumed == buffer_.size()) {
      buffer_.clear();
      return events;
    }
  }
}

void FrameCodec::reset() {
  buffer_.clear();
}

}  // namespace lattice
