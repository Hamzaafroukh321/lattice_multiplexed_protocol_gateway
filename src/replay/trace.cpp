#include "lattice/trace.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace lattice {
namespace {

[[nodiscard]] const char* trace_kind_name(TraceKind kind) {
  switch (kind) {
    case TraceKind::transport_bytes: return "transport";
    case TraceKind::api_event: return "api";
    case TraceKind::timer: return "timer";
    case TraceKind::plugin_completion: return "plugin";
    case TraceKind::diagnostic: return "diagnostic";
  }
  return "diagnostic";
}

[[nodiscard]] Result<TraceKind> parse_kind(const std::string& text) {
  if (text == "transport") {
    return TraceKind::transport_bytes;
  }
  if (text == "api") {
    return TraceKind::api_event;
  }
  if (text == "timer") {
    return TraceKind::timer;
  }
  if (text == "plugin") {
    return TraceKind::plugin_completion;
  }
  if (text == "diagnostic") {
    return TraceKind::diagnostic;
  }
  return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                    "unknown trace event kind");
}

[[nodiscard]] std::string hex(const Bytes& bytes) {
  std::ostringstream out;
  for (std::uint8_t byte : bytes) {
    out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
  }
  return out.str();
}

[[nodiscard]] Result<Bytes> parse_hex(const std::string& text) {
  if ((text.size() % 2U) != 0U) {
    return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                      "trace hex has odd length");
  }
  Bytes out;
  out.reserve(text.size() / 2U);
  for (std::size_t i = 0; i < text.size(); i += 2U) {
    const auto parse_nibble = [](char ch) -> int {
      if (ch >= '0' && ch <= '9') {
        return ch - '0';
      }
      if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
      }
      if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
      }
      return -1;
    };
    const int high = parse_nibble(text[i]);
    const int low = parse_nibble(text[i + 1U]);
    if (high < 0 || low < 0) {
      return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                        "trace hex contains non-hex byte");
    }
    out.push_back(static_cast<std::uint8_t>((high << 4) | low));
  }
  return out;
}

[[nodiscard]] Result<std::uint64_t> parse_u64(const std::string& text) {
  if (text.empty()) {
    return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                      "trace timestamp is empty");
  }
  std::uint64_t value = 0;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                        "trace timestamp is not decimal");
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
      return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                        "trace timestamp overflows u64");
    }
    value = value * 10U + digit;
  }
  return value;
}

}  // namespace

void TraceLog::record(TraceEvent event) {
  events_.push_back(std::move(event));
  std::stable_sort(events_.begin(), events_.end(), [](const TraceEvent& a, const TraceEvent& b) {
    return a.time_ms < b.time_ms;
  });
}

Result<std::string> TraceLog::serialize() const {
  std::ostringstream out;
  out << "LTXTRACE/1\n";
  for (const TraceEvent& event : events_) {
    if (event.label.find('|') != std::string::npos || event.label.find('\n') != std::string::npos) {
      return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                        "trace labels cannot contain separators");
    }
    out << event.time_ms << '|' << trace_kind_name(event.kind) << '|' << event.label << '|'
        << hex(event.bytes) << '\n';
  }
  return out.str();
}

Result<TraceLog> TraceLog::parse(const std::string& text) {
  std::istringstream in(text);
  std::string line;
  if (!std::getline(in, line) || line != "LTXTRACE/1") {
    return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                      "trace header mismatch");
  }
  TraceLog log;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    const std::size_t first = line.find('|');
    const std::size_t second = first == std::string::npos ? std::string::npos : line.find('|', first + 1U);
    const std::size_t third = second == std::string::npos ? std::string::npos : line.find('|', second + 1U);
    if (first == std::string::npos || second == std::string::npos || third == std::string::npos) {
      return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                        "malformed trace line");
    }
    TraceEvent event;
    auto time = parse_u64(line.substr(0U, first));
    if (!time) {
      return time.error();
    }
    event.time_ms = time.value();
    auto kind = parse_kind(line.substr(first + 1U, second - first - 1U));
    if (!kind) {
      return kind.error();
    }
    event.kind = kind.value();
    event.label = line.substr(second + 1U, third - second - 1U);
    auto bytes = parse_hex(line.substr(third + 1U));
    if (!bytes) {
      return bytes.error();
    }
    event.bytes = bytes.value();
    log.record(std::move(event));
  }
  return log;
}

}  // namespace lattice
