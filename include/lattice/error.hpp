#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace lattice {

enum class ErrorScope : std::uint8_t {
  connection,
  channel,
  message,
  plugin,
  transport,
  internal
};

enum class ErrorCode : std::uint16_t {
  need_more_data,
  truncated_frame,
  bad_magic,
  reserved_flags,
  varint_non_canonical,
  varint_too_long,
  frame_too_large,
  header_too_large,
  crc_mismatch,
  malformed_tlv,
  unsupported_frame_type,
  negotiation_rejected,
  duplicate_required_tlv,
  unknown_required_feature,
  stale_generation,
  illegal_state,
  fragment_range,
  fragment_overlap,
  sequence_error,
  flow_underflow,
  flow_overflow,
  resource_limit,
  would_block,
  plugin_decode,
  resume_rejected,
  timeout,
  cancelled,
  transport_error,
  invariant_failure
};

enum class CloseAction : std::uint8_t {
  none,
  reject_message,
  reset_channel,
  close_connection
};

struct Error {
  ErrorScope scope{ErrorScope::internal};
  ErrorCode code{ErrorCode::invariant_failure};
  CloseAction action{CloseAction::none};
  std::uint32_t channel_no{0};
  std::uint8_t generation{0};
  std::uint32_t frame_seq{0};
  std::uint32_t message_seq{0};
  std::uint64_t offset{0};
  std::string detail;

  [[nodiscard]] std::string stable_code() const;
};

template <typename T>
class Result {
 public:
  Result(const T& value) : storage_(value) {}
  Result(T&& value) : storage_(std::move(value)) {}
  Result(Error error) : storage_(std::move(error)) {}

  [[nodiscard]] bool ok() const { return std::holds_alternative<T>(storage_); }
  explicit operator bool() const { return ok(); }
  [[nodiscard]] const T& value() const { return std::get<T>(storage_); }
  [[nodiscard]] T& value() { return std::get<T>(storage_); }
  [[nodiscard]] T take_value() { return std::move(std::get<T>(storage_)); }
  [[nodiscard]] const Error& error() const { return std::get<Error>(storage_); }

 private:
  std::variant<T, Error> storage_;
};

template <>
class Result<void> {
 public:
  Result() : error_(std::nullopt) {}
  Result(Error error) : error_(std::move(error)) {}

  [[nodiscard]] bool ok() const { return !error_.has_value(); }
  explicit operator bool() const { return ok(); }
  [[nodiscard]] const Error& error() const { return *error_; }

 private:
  std::optional<Error> error_;
};

[[nodiscard]] Error make_error(ErrorScope scope, ErrorCode code, CloseAction action,
                               std::string detail);

}  // namespace lattice
