#include "lattice/inspection.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace lattice {
namespace {

constexpr std::uint16_t kExtMessageSeq = 1U;
constexpr std::uint16_t kExtFragmentOffset = 2U;
constexpr std::uint16_t kExtMessageTotal = 3U;
constexpr std::uint16_t kExtFamilyId = 4U;

[[nodiscard]] bool is_control_payload_frame(FrameType type) {
  switch (type) {
    case FrameType::hello:
    case FrameType::ack:
    case FrameType::ping:
    case FrameType::pong:
    case FrameType::goaway:
    case FrameType::resume:
      return true;
    case FrameType::open:
    case FrameType::data:
    case FrameType::credit:
    case FrameType::half_close:
    case FrameType::reset:
      return false;
  }
  return false;
}

[[nodiscard]] bool is_data_carrier(FrameType type) {
  return type == FrameType::data;
}

void add_finding(std::vector<FrameFinding>& findings, InspectionSeverity severity,
                 FrameFindingCode code, const Frame& frame, std::size_t index,
                 std::string detail) {
  findings.push_back(FrameFinding{severity, code, index, frame.frame_seq, frame.channel,
                                  std::move(detail)});
}

void add_data_extension_finding(std::vector<FrameFinding>& findings, const Frame& frame,
                                std::size_t index, std::uint16_t type,
                                FrameFindingCode missing_code, const char* name) {
  const auto ext = find_extension(frame, type);
  if (!ext.has_value()) {
    add_finding(findings, InspectionSeverity::error, missing_code, frame, index,
                std::string("DATA frame is missing ") + name + " extension");
    return;
  }
  if (ext->value.size() != 4U) {
    add_finding(findings, InspectionSeverity::error,
                FrameFindingCode::invalid_message_extension, frame, index,
                std::string(name) + " extension must be exactly four bytes");
  }
}

[[nodiscard]] std::string channel_list(const std::vector<ChannelId>& channels) {
  std::ostringstream out;
  for (std::size_t i = 0; i < channels.size(); ++i) {
    if (i != 0U) {
      out << ',';
    }
    out << channels[i].str();
  }
  return out.str();
}

}  // namespace

std::size_t StreamInspectionReport::error_count() const {
  return static_cast<std::size_t>(std::count_if(
      findings.begin(), findings.end(), [](const FrameFinding& finding) {
        return finding.severity == InspectionSeverity::error;
      }));
}

std::size_t StreamInspectionReport::warning_count() const {
  return static_cast<std::size_t>(std::count_if(
      findings.begin(), findings.end(), [](const FrameFinding& finding) {
        return finding.severity == InspectionSeverity::warning;
      }));
}

const char* to_string(InspectionSeverity severity) {
  switch (severity) {
    case InspectionSeverity::info:
      return "info";
    case InspectionSeverity::warning:
      return "warning";
    case InspectionSeverity::error:
      return "error";
  }
  return "unknown";
}

const char* to_string(FrameFindingCode code) {
  switch (code) {
    case FrameFindingCode::decoder_error:
      return "decoder_error";
    case FrameFindingCode::too_many_frames:
      return "too_many_frames";
    case FrameFindingCode::empty_required_extension:
      return "empty_required_extension";
    case FrameFindingCode::duplicate_extension_type:
      return "duplicate_extension_type";
    case FrameFindingCode::missing_message_sequence:
      return "missing_message_sequence";
    case FrameFindingCode::missing_fragment_offset:
      return "missing_fragment_offset";
    case FrameFindingCode::missing_message_total:
      return "missing_message_total";
    case FrameFindingCode::missing_family_id:
      return "missing_family_id";
    case FrameFindingCode::invalid_message_extension:
      return "invalid_message_extension";
    case FrameFindingCode::zero_message_total:
      return "zero_message_total";
    case FrameFindingCode::fragment_exceeds_total:
      return "fragment_exceeds_total";
    case FrameFindingCode::control_payload_large:
      return "control_payload_large";
    case FrameFindingCode::sequence_rewind:
      return "sequence_rewind";
    case FrameFindingCode::goaway_not_last:
      return "goaway_not_last";
    case FrameFindingCode::resume_on_data_channel:
      return "resume_on_data_channel";
    case FrameFindingCode::ping_payload_size:
      return "ping_payload_size";
    case FrameFindingCode::pong_payload_size:
      return "pong_payload_size";
  }
  return "unknown";
}

const char* to_string(FrameType type) {
  switch (type) {
    case FrameType::hello:
      return "HELLO";
    case FrameType::open:
      return "OPEN";
    case FrameType::data:
      return "DATA";
    case FrameType::ack:
      return "ACK";
    case FrameType::credit:
      return "CREDIT";
    case FrameType::half_close:
      return "HALF_CLOSE";
    case FrameType::reset:
      return "RESET";
    case FrameType::ping:
      return "PING";
    case FrameType::pong:
      return "PONG";
    case FrameType::goaway:
      return "GOAWAY";
    case FrameType::resume:
      return "RESUME";
  }
  return "UNKNOWN";
}

FrameObservation observe_frame(const Frame& frame, std::size_t index) {
  FrameObservation observation;
  observation.index = index;
  observation.type = frame.type;
  observation.channel = frame.channel;
  observation.frame_seq = frame.frame_seq;
  observation.payload_bytes = frame.payload.size();
  observation.extension_count = frame.extensions.size();
  observation.extensions.reserve(frame.extensions.size());
  for (const Extension& ext : frame.extensions) {
    if (ext.required) {
      ++observation.required_extension_count;
    }
    observation.extensions.push_back(ExtensionObservation{ext.type, ext.required,
                                                         ext.value.size()});
  }
  return observation;
}

std::vector<FrameFinding> inspect_frame(const Frame& frame, std::size_t index) {
  std::vector<FrameFinding> findings;
  std::map<std::uint16_t, std::size_t> seen;
  for (const Extension& ext : frame.extensions) {
    ++seen[ext.type];
    if (ext.required && ext.value.empty()) {
      add_finding(findings, InspectionSeverity::warning,
                  FrameFindingCode::empty_required_extension, frame, index,
                  "required extension has an empty value");
    }
  }
  for (const auto& [type, count] : seen) {
    if (count > 1U) {
      add_finding(findings, InspectionSeverity::error,
                  FrameFindingCode::duplicate_extension_type, frame, index,
                  "frame repeats extension type " + std::to_string(type));
    }
  }
  if (is_data_carrier(frame.type)) {
    add_data_extension_finding(findings, frame, index, kExtMessageSeq,
                               FrameFindingCode::missing_message_sequence,
                               "message sequence");
    add_data_extension_finding(findings, frame, index, kExtFragmentOffset,
                               FrameFindingCode::missing_fragment_offset,
                               "fragment offset");
    add_data_extension_finding(findings, frame, index, kExtMessageTotal,
                               FrameFindingCode::missing_message_total,
                               "message total");
    add_data_extension_finding(findings, frame, index, kExtFamilyId,
                               FrameFindingCode::missing_family_id,
                               "family id");
    auto total = extension_u32(frame, kExtMessageTotal);
    auto offset = extension_u32(frame, kExtFragmentOffset);
    if (total && total.value() == 0U) {
      add_finding(findings, InspectionSeverity::error,
                  FrameFindingCode::zero_message_total, frame, index,
                  "message total cannot be zero");
    }
    if (total && offset && offset.value() > total.value()) {
      add_finding(findings, InspectionSeverity::error,
                  FrameFindingCode::fragment_exceeds_total, frame, index,
                  "fragment offset exceeds declared total");
    }
  }
  if ((frame.type == FrameType::ping || frame.type == FrameType::pong) &&
      frame.payload.size() != 8U) {
    add_finding(findings, InspectionSeverity::warning,
                frame.type == FrameType::ping ? FrameFindingCode::ping_payload_size
                                              : FrameFindingCode::pong_payload_size,
                frame, index, "PING/PONG payload should be exactly eight bytes");
  }
  if (frame.type == FrameType::resume && !frame.channel.is_control()) {
    add_finding(findings, InspectionSeverity::error,
                FrameFindingCode::resume_on_data_channel, frame, index,
                "RESUME must use the control channel");
  }
  if (is_control_payload_frame(frame.type) && frame.payload.size() > 512U) {
    add_finding(findings, InspectionSeverity::warning,
                FrameFindingCode::control_payload_large, frame, index,
                "control payload is unusually large");
  }
  return findings;
}

StreamInspectionReport inspect_frame_stream(std::span<const std::uint8_t> bytes,
                                            StreamInspectionOptions options) {
  StreamInspectionReport report;
  report.decoded_bytes = bytes.size();
  report.decoder_reached_eof = options.eof;
  FrameCodec codec(options.limits);
  std::optional<std::uint32_t> previous_sequence;
  const auto events = codec.feed(bytes, options.eof);
  for (const DecodeEvent& event : events) {
    if (event.status == DecodeStatus::need_more) {
      continue;
    }
    if (event.status == DecodeStatus::error) {
      FrameFinding finding;
      finding.severity = InspectionSeverity::error;
      finding.code = FrameFindingCode::decoder_error;
      finding.frame_index = report.frames.size();
      finding.detail = event.error ? event.error->detail : "decoder rejected input";
      report.findings.push_back(std::move(finding));
      continue;
    }
    const Frame& frame = event.frame.value();
    if (report.frames.size() >= options.max_frames) {
      add_finding(report.findings, InspectionSeverity::error,
                  FrameFindingCode::too_many_frames, frame, report.frames.size(),
                  "stream contains more frames than the inspection cap");
      break;
    }
    if (options.warn_on_sequence_rewind && previous_sequence.has_value() &&
        frame.frame_seq < previous_sequence.value()) {
      add_finding(report.findings, InspectionSeverity::warning,
                  FrameFindingCode::sequence_rewind, frame, report.frames.size(),
                  "frame sequence moved backwards");
    }
    previous_sequence = frame.frame_seq;
    if (options.warn_on_goaway_not_last && frame.type == FrameType::goaway &&
        &event != &events.back()) {
      add_finding(report.findings, InspectionSeverity::warning,
                  FrameFindingCode::goaway_not_last, frame, report.frames.size(),
                  "GOAWAY was followed by additional decoder events");
    }
    report.payload_bytes += frame.payload.size();
    if (frame.type == FrameType::data) {
      ++report.data_frames;
    } else {
      ++report.control_frames;
    }
    if (options.require_data_message_extensions || frame.type != FrameType::data) {
      std::vector<FrameFinding> frame_findings = inspect_frame(frame, report.frames.size());
      report.findings.insert(report.findings.end(), frame_findings.begin(),
                             frame_findings.end());
    }
    report.frames.push_back(observe_frame(frame, report.frames.size()));
  }
  return report;
}

ConnectionEventSummary summarize_connection_events(const std::vector<ConnectionEvent>& events) {
  ConnectionEventSummary summary;
  std::map<std::string, ChannelId> channels;
  summary.total = events.size();
  for (const ConnectionEvent& event : events) {
    summary.payload_bytes += event.payload.size();
    if (!event.channel.is_control()) {
      channels[event.channel.str()] = event.channel;
    }
    switch (event.kind) {
      case ConnectionEvent::Kind::negotiated:
        ++summary.negotiated;
        break;
      case ConnectionEvent::Kind::channel_opened:
        ++summary.opened;
        break;
      case ConnectionEvent::Kind::message_delivered:
        ++summary.delivered;
        break;
      case ConnectionEvent::Kind::plugin_response:
        ++summary.plugin_responses;
        break;
      case ConnectionEvent::Kind::pong_received:
        ++summary.pongs;
        break;
      case ConnectionEvent::Kind::resumed:
        ++summary.resumed;
        break;
      case ConnectionEvent::Kind::timer_expired:
        ++summary.timers;
        break;
      case ConnectionEvent::Kind::channel_reset:
        ++summary.resets;
        break;
      case ConnectionEvent::Kind::half_closed:
        ++summary.half_closes;
        break;
      case ConnectionEvent::Kind::closed:
        ++summary.closed;
        break;
      case ConnectionEvent::Kind::diagnostic:
        ++summary.diagnostics;
        break;
    }
  }
  summary.channels.reserve(channels.size());
  for (const auto& [name, id] : channels) {
    (void)name;
    summary.channels.push_back(id);
  }
  return summary;
}

std::string format_inspection_report(const StreamInspectionReport& report) {
  std::ostringstream out;
  out << "frames=" << report.frames.size()
      << " data=" << report.data_frames
      << " control=" << report.control_frames
      << " payload_bytes=" << report.payload_bytes
      << " errors=" << report.error_count()
      << " warnings=" << report.warning_count() << '\n';
  for (const FrameObservation& frame : report.frames) {
    out << "frame[" << frame.index << "]"
        << " type=" << to_string(frame.type)
        << " channel=" << frame.channel.str()
        << " seq=" << frame.frame_seq
        << " payload=" << frame.payload_bytes
        << " extensions=" << frame.extension_count
        << " required=" << frame.required_extension_count << '\n';
  }
  for (const FrameFinding& finding : report.findings) {
    out << to_string(finding.severity)
        << " frame=" << finding.frame_index
        << " code=" << to_string(finding.code)
        << " detail=\"" << finding.detail << "\"\n";
  }
  return out.str();
}

std::string format_connection_event_summary(const ConnectionEventSummary& summary) {
  std::ostringstream out;
  out << "events=" << summary.total
      << " negotiated=" << summary.negotiated
      << " opened=" << summary.opened
      << " delivered=" << summary.delivered
      << " plugin=" << summary.plugin_responses
      << " pongs=" << summary.pongs
      << " resumed=" << summary.resumed
      << " timers=" << summary.timers
      << " resets=" << summary.resets
      << " half_closes=" << summary.half_closes
      << " closed=" << summary.closed
      << " diagnostics=" << summary.diagnostics
      << " payload_bytes=" << summary.payload_bytes
      << " channels=" << channel_list(summary.channels);
  return out.str();
}

}  // namespace lattice
