#pragma once

#include "lattice/error.hpp"
#include "lattice/types.hpp"

#include <string>

namespace lattice {

enum class TraceKind : std::uint8_t {
  transport_bytes,
  api_event,
  timer,
  plugin_completion,
  diagnostic
};

struct TraceEvent {
  std::uint64_t time_ms{0};
  TraceKind kind{TraceKind::api_event};
  std::string label;
  Bytes bytes;
};

struct TraceReplayReport {
  std::size_t total_events{0};
  std::size_t transport_events{0};
  std::size_t api_events{0};
  std::size_t timer_events{0};
  std::size_t plugin_events{0};
  std::size_t diagnostic_events{0};
  std::size_t total_bytes{0};
  bool canonical{false};
};

class TraceLog {
 public:
  void record(TraceEvent event);
  [[nodiscard]] const std::vector<TraceEvent>& events() const { return events_; }
  [[nodiscard]] Result<std::string> serialize() const;
  [[nodiscard]] static Result<TraceLog> parse(const std::string& text);

 private:
  std::vector<TraceEvent> events_;
};

[[nodiscard]] Result<TraceReplayReport> verify_trace_replay(const TraceLog& log);

}  // namespace lattice
