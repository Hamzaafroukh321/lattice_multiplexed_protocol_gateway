#pragma once

#include "lattice/connection.hpp"
#include "lattice/frame.hpp"

#include <span>
#include <string>
#include <vector>

namespace lattice {

enum class InspectionSeverity : std::uint8_t {
  info,
  warning,
  error
};

enum class FrameFindingCode : std::uint8_t {
  decoder_error,
  too_many_frames,
  empty_required_extension,
  duplicate_extension_type,
  missing_message_sequence,
  missing_fragment_offset,
  missing_message_total,
  missing_family_id,
  invalid_message_extension,
  zero_message_total,
  fragment_exceeds_total,
  control_payload_large,
  sequence_rewind,
  goaway_not_last,
  resume_on_data_channel,
  ping_payload_size,
  pong_payload_size
};

struct FrameFinding {
  InspectionSeverity severity{InspectionSeverity::warning};
  FrameFindingCode code{FrameFindingCode::decoder_error};
  std::size_t frame_index{0};
  std::uint32_t frame_seq{0};
  ChannelId channel;
  std::string detail;
};

struct ExtensionObservation {
  std::uint16_t type{0};
  bool required{false};
  std::size_t value_bytes{0};
};

struct FrameObservation {
  std::size_t index{0};
  FrameType type{FrameType::hello};
  ChannelId channel;
  std::uint32_t frame_seq{0};
  std::size_t payload_bytes{0};
  std::size_t extension_count{0};
  std::size_t required_extension_count{0};
  std::vector<ExtensionObservation> extensions;
};

struct StreamInspectionOptions {
  FrameLimits limits;
  bool eof{true};
  bool require_data_message_extensions{true};
  bool warn_on_sequence_rewind{true};
  bool warn_on_goaway_not_last{true};
  std::size_t max_frames{4096};
};

struct StreamInspectionReport {
  std::vector<FrameObservation> frames;
  std::vector<FrameFinding> findings;
  std::size_t decoded_bytes{0};
  std::size_t payload_bytes{0};
  std::size_t data_frames{0};
  std::size_t control_frames{0};
  bool decoder_reached_eof{false};

  [[nodiscard]] std::size_t error_count() const;
  [[nodiscard]] std::size_t warning_count() const;
  [[nodiscard]] bool ok() const { return error_count() == 0U; }
};

struct ConnectionEventSummary {
  std::size_t total{0};
  std::size_t negotiated{0};
  std::size_t opened{0};
  std::size_t delivered{0};
  std::size_t plugin_responses{0};
  std::size_t pongs{0};
  std::size_t resumed{0};
  std::size_t timers{0};
  std::size_t resets{0};
  std::size_t half_closes{0};
  std::size_t closed{0};
  std::size_t diagnostics{0};
  std::size_t payload_bytes{0};
  std::vector<ChannelId> channels;
};

[[nodiscard]] const char* to_string(InspectionSeverity severity);
[[nodiscard]] const char* to_string(FrameFindingCode code);
[[nodiscard]] const char* to_string(FrameType type);

[[nodiscard]] FrameObservation observe_frame(const Frame& frame, std::size_t index);
[[nodiscard]] std::vector<FrameFinding> inspect_frame(const Frame& frame, std::size_t index);
[[nodiscard]] StreamInspectionReport inspect_frame_stream(
    std::span<const std::uint8_t> bytes,
    StreamInspectionOptions options = {});
[[nodiscard]] ConnectionEventSummary summarize_connection_events(
    const std::vector<ConnectionEvent>& events);
[[nodiscard]] std::string format_inspection_report(const StreamInspectionReport& report);
[[nodiscard]] std::string format_connection_event_summary(const ConnectionEventSummary& summary);

}  // namespace lattice
