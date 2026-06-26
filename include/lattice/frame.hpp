#pragma once

#include "lattice/error.hpp"
#include "lattice/types.hpp"

#include <deque>
#include <optional>
#include <span>

namespace lattice {

struct Extension {
  std::uint16_t type{0};
  bool required{false};
  Bytes value;

  friend bool operator==(const Extension&, const Extension&) = default;
};

struct Frame {
  FrameType type{FrameType::hello};
  std::uint8_t flags{0};
  ChannelId channel{};
  std::uint32_t frame_seq{0};
  std::vector<Extension> extensions;
  Bytes payload;
};

enum class DecodeStatus : std::uint8_t {
  frame,
  need_more,
  error
};

struct DecodeEvent {
  DecodeStatus status{DecodeStatus::need_more};
  std::optional<Frame> frame;
  std::optional<Error> error;
};

struct FrameLimits {
  std::size_t max_frame{kDefaultMaxFrame};
  std::size_t max_header{4096};
};

class FrameCodec {
 public:
  explicit FrameCodec(FrameLimits limits = {});

  [[nodiscard]] Result<Bytes> encode(const Frame& frame) const;
  [[nodiscard]] std::vector<DecodeEvent> feed(std::span<const std::uint8_t> bytes, bool eof);
  void reset();

 private:
  FrameLimits limits_;
  Bytes buffer_;
};

[[nodiscard]] Result<std::uint32_t> read_u32_be(std::span<const std::uint8_t> bytes,
                                                std::size_t offset);
[[nodiscard]] std::uint32_t crc32c(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<Bytes> encode_uleb128(std::uint32_t value);
[[nodiscard]] Result<std::pair<std::uint32_t, std::size_t>> decode_uleb128(
    std::span<const std::uint8_t> bytes, std::size_t offset);
[[nodiscard]] Result<std::vector<Extension>> parse_extensions(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<Bytes> write_extensions(std::vector<Extension> extensions);
[[nodiscard]] std::optional<Extension> find_extension(const Frame& frame, std::uint16_t type);
[[nodiscard]] Result<std::uint32_t> extension_u32(const Frame& frame, std::uint16_t type);

}  // namespace lattice
