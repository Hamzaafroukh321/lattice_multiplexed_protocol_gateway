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

class TraceLog {
 public:
  void record(TraceEvent event);
  [[nodiscard]] const std::vector<TraceEvent>& events() const { return events_; }
  [[nodiscard]] Result<std::string> serialize() const;
  [[nodiscard]] static Result<TraceLog> parse(const std::string& text);

 private:
  std::vector<TraceEvent> events_;
};

}  // namespace lattice
