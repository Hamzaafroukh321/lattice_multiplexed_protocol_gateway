#include "lattice/route_policy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <sstream>

namespace lattice {
namespace {

[[nodiscard]] Error policy_error(RoutePolicyDiagnosticCode code, std::size_t line,
                                 std::string detail) {
  ErrorCode error_code = ErrorCode::resource_limit;
  switch (code) {
    case RoutePolicyDiagnosticCode::invalid_channel:
      error_code = ErrorCode::stale_generation;
      break;
    case RoutePolicyDiagnosticCode::invalid_hex:
      error_code = ErrorCode::malformed_tlv;
      break;
    case RoutePolicyDiagnosticCode::malformed_line:
    case RoutePolicyDiagnosticCode::unknown_key:
    case RoutePolicyDiagnosticCode::missing_key:
    case RoutePolicyDiagnosticCode::duplicate_key:
    case RoutePolicyDiagnosticCode::empty_route:
    case RoutePolicyDiagnosticCode::duplicate_source:
    case RoutePolicyDiagnosticCode::duplicate_name:
    case RoutePolicyDiagnosticCode::invalid_integer:
      error_code = ErrorCode::resource_limit;
      break;
  }
  return make_error(ErrorScope::connection, error_code, CloseAction::none,
                    "route policy line " + std::to_string(line) + ": " + detail);
}

[[nodiscard]] std::string trim(std::string text) {
  const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
  return text;
}

[[nodiscard]] std::string unquote(std::string text) {
  text = trim(std::move(text));
  if (text.size() >= 2U && text.front() == '"' && text.back() == '"') {
    return text.substr(1U, text.size() - 2U);
  }
  return text;
}

[[nodiscard]] Result<std::uint32_t> parse_u32_text(const std::string& text,
                                                   std::size_t line) {
  if (text.empty()) {
    return policy_error(RoutePolicyDiagnosticCode::invalid_integer, line,
                        "integer value is empty");
  }
  std::uint64_t value = 0;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return policy_error(RoutePolicyDiagnosticCode::invalid_integer, line,
                          "integer value is not decimal");
    }
    value = value * 10U + static_cast<std::uint64_t>(ch - '0');
    if (value > 0xFFFFFFFFULL) {
      return policy_error(RoutePolicyDiagnosticCode::invalid_integer, line,
                          "integer value overflows u32");
    }
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] Result<ChannelId> parse_channel_text(std::string text, std::size_t line) {
  text = unquote(std::move(text));
  const std::size_t colon = text.find(':');
  if (colon == std::string::npos) {
    return policy_error(RoutePolicyDiagnosticCode::invalid_channel, line,
                        "channel must use number:generation syntax");
  }
  auto number = parse_u32_text(text.substr(0U, colon), line);
  if (!number) {
    return number.error();
  }
  auto generation = parse_u32_text(text.substr(colon + 1U), line);
  if (!generation) {
    return generation.error();
  }
  if (generation.value() > 0xFFU) {
    return policy_error(RoutePolicyDiagnosticCode::invalid_channel, line,
                        "channel generation exceeds u8");
  }
  if (number.value() == 0U && generation.value() != 0U) {
    return policy_error(RoutePolicyDiagnosticCode::invalid_channel, line,
                        "control channel must use generation zero");
  }
  return ChannelId{number.value(), static_cast<std::uint8_t>(generation.value())};
}

[[nodiscard]] int hex_nibble(char ch) {
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
}

[[nodiscard]] Result<Bytes> parse_hex_text(std::string text, std::size_t line) {
  text = unquote(std::move(text));
  if ((text.size() % 2U) != 0U) {
    return policy_error(RoutePolicyDiagnosticCode::invalid_hex, line,
                        "hex payload has an odd number of characters");
  }
  Bytes out;
  out.reserve(text.size() / 2U);
  for (std::size_t i = 0; i < text.size(); i += 2U) {
    const int high = hex_nibble(text[i]);
    const int low = hex_nibble(text[i + 1U]);
    if (high < 0 || low < 0) {
      return policy_error(RoutePolicyDiagnosticCode::invalid_hex, line,
                          "hex payload contains a non-hex character");
    }
    out.push_back(static_cast<std::uint8_t>(
        (static_cast<unsigned>(high) << 4U) | static_cast<unsigned>(low)));
  }
  return out;
}

[[nodiscard]] std::string hex_encode(std::span<const std::uint8_t> bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2U);
  for (std::uint8_t byte : bytes) {
    out.push_back(kHex[(byte >> 4U) & 0x0FU]);
    out.push_back(kHex[byte & 0x0FU]);
  }
  return out;
}

struct ParseState {
  RoutePolicyDocument document;
  RoutePolicyEntry current;
  std::set<std::string> keys;
  bool in_route{false};
  std::size_t route_start_line{0};
};

[[nodiscard]] Result<void> finish_route(ParseState& state) {
  if (!state.in_route) {
    return {};
  }
  if (state.keys.empty()) {
    return policy_error(RoutePolicyDiagnosticCode::empty_route, state.route_start_line,
                        "route section has no keys");
  }
  const std::array<const char*, 3U> required{"source", "destination", "family"};
  for (const char* key : required) {
    if (state.keys.find(key) == state.keys.end()) {
      return policy_error(RoutePolicyDiagnosticCode::missing_key, state.route_start_line,
                          std::string("route is missing ") + key);
    }
  }
  if (state.current.payload.empty()) {
    state.current.payload = Bytes{'o', 'k'};
  }
  state.document.routes.push_back(std::move(state.current));
  state.current = RoutePolicyEntry{};
  state.keys.clear();
  state.in_route = false;
  state.route_start_line = 0U;
  return {};
}

[[nodiscard]] Result<void> begin_route(ParseState& state, std::size_t line) {
  auto finished = finish_route(state);
  if (!finished) {
    return finished.error();
  }
  state.current = RoutePolicyEntry{};
  state.keys.clear();
  state.in_route = true;
  state.route_start_line = line;
  return {};
}

[[nodiscard]] Result<void> set_key(ParseState& state, std::string key,
                                   std::string value, std::size_t line) {
  if (!state.in_route) {
    auto started = begin_route(state, line);
    if (!started) {
      return started.error();
    }
  }
  if (!state.keys.insert(key).second) {
    return policy_error(RoutePolicyDiagnosticCode::duplicate_key, line,
                        "route repeats key " + key);
  }
  if (key == "name") {
    state.current.name = unquote(std::move(value));
    return {};
  }
  if (key == "source") {
    auto parsed = parse_channel_text(std::move(value), line);
    if (!parsed) {
      return parsed.error();
    }
    state.current.source = parsed.value();
    return {};
  }
  if (key == "destination") {
    auto parsed = parse_channel_text(std::move(value), line);
    if (!parsed) {
      return parsed.error();
    }
    state.current.destination = parsed.value();
    return {};
  }
  if (key == "family") {
    auto parsed = parse_u32_text(unquote(std::move(value)), line);
    if (!parsed) {
      return parsed.error();
    }
    state.current.family_id = parsed.value();
    return {};
  }
  if (key == "payload_hex") {
    auto parsed = parse_hex_text(std::move(value), line);
    if (!parsed) {
      return parsed.error();
    }
    state.current.payload = parsed.value();
    return {};
  }
  return policy_error(RoutePolicyDiagnosticCode::unknown_key, line,
                      "unknown route key " + key);
}

}  // namespace

const char* to_string(RoutePolicyDiagnosticCode code) {
  switch (code) {
    case RoutePolicyDiagnosticCode::missing_key:
      return "missing_key";
    case RoutePolicyDiagnosticCode::duplicate_key:
      return "duplicate_key";
    case RoutePolicyDiagnosticCode::malformed_line:
      return "malformed_line";
    case RoutePolicyDiagnosticCode::invalid_integer:
      return "invalid_integer";
    case RoutePolicyDiagnosticCode::invalid_channel:
      return "invalid_channel";
    case RoutePolicyDiagnosticCode::invalid_hex:
      return "invalid_hex";
    case RoutePolicyDiagnosticCode::empty_route:
      return "empty_route";
    case RoutePolicyDiagnosticCode::duplicate_source:
      return "duplicate_source";
    case RoutePolicyDiagnosticCode::duplicate_name:
      return "duplicate_name";
    case RoutePolicyDiagnosticCode::unknown_key:
      return "unknown_key";
  }
  return "unknown";
}

Result<RoutePolicyDocument> parse_route_policy_text(const std::string& text) {
  ParseState state;
  std::istringstream in(text);
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    const std::size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line = line.substr(0U, comment);
    }
    line = trim(std::move(line));
    if (line.empty()) {
      continue;
    }
    if (line == "[route]") {
      auto started = begin_route(state, line_no);
      if (!started) {
        return started.error();
      }
      continue;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
      return policy_error(RoutePolicyDiagnosticCode::malformed_line, line_no,
                          "expected key=value");
    }
    std::string key = trim(line.substr(0U, equals));
    std::string value = trim(line.substr(equals + 1U));
    auto set = set_key(state, std::move(key), std::move(value), line_no);
    if (!set) {
      return set.error();
    }
  }
  auto finished = finish_route(state);
  if (!finished) {
    return finished.error();
  }
  if (state.document.routes.empty()) {
    return policy_error(RoutePolicyDiagnosticCode::empty_route, line_no,
                        "policy contains no routes");
  }
  auto validation = validate_route_policy(state.document);
  if (!validation.ok()) {
    const RoutePolicyDiagnostic& diagnostic = validation.diagnostics.front();
    return policy_error(diagnostic.code, diagnostic.line, diagnostic.detail);
  }
  return state.document;
}

Result<RoutePolicyDocument> parse_route_policy_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return make_error(ErrorScope::connection, ErrorCode::resource_limit, CloseAction::none,
                      "cannot open route policy file");
  }
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return parse_route_policy_text(text);
}

RoutePolicyValidation validate_route_policy(const RoutePolicyDocument& document) {
  RoutePolicyValidation validation;
  std::map<std::string, std::size_t> names;
  std::map<std::string, std::size_t> sources;
  for (std::size_t i = 0; i < document.routes.size(); ++i) {
    const RoutePolicyEntry& route = document.routes[i];
    const std::size_t line = i + 1U;
    if (route.source.is_control() || route.destination.is_control()) {
      validation.diagnostics.push_back(RoutePolicyDiagnostic{
          RoutePolicyDiagnosticCode::invalid_channel, line,
          "source and destination must be data channels"});
    }
    if (route.family_id == 0U) {
      validation.diagnostics.push_back(RoutePolicyDiagnostic{
          RoutePolicyDiagnosticCode::invalid_integer, line,
          "family id must be nonzero"});
    }
    const std::string source_key = route.source.str();
    const auto [source_it, source_inserted] = sources.emplace(source_key, line);
    if (!source_inserted) {
      validation.diagnostics.push_back(RoutePolicyDiagnostic{
          RoutePolicyDiagnosticCode::duplicate_source, line,
          "source channel duplicates route at entry " + std::to_string(source_it->second)});
    }
    if (!route.name.empty()) {
      const auto [name_it, name_inserted] = names.emplace(route.name, line);
      if (!name_inserted) {
        validation.diagnostics.push_back(RoutePolicyDiagnostic{
            RoutePolicyDiagnosticCode::duplicate_name, line,
            "route name duplicates entry " + std::to_string(name_it->second)});
      }
    }
  }
  if (document.routes.empty()) {
    validation.diagnostics.push_back(RoutePolicyDiagnostic{
        RoutePolicyDiagnosticCode::empty_route, 0U, "policy contains no routes"});
  }
  return validation;
}

Result<std::string> serialize_route_policy(const RoutePolicyDocument& document) {
  auto validation = validate_route_policy(document);
  if (!validation.ok()) {
    const RoutePolicyDiagnostic& diagnostic = validation.diagnostics.front();
    return policy_error(diagnostic.code, diagnostic.line, diagnostic.detail);
  }
  std::ostringstream out;
  for (const RoutePolicyEntry& route : document.routes) {
    out << "[route]\n";
    if (!route.name.empty()) {
      out << "name=\"" << route.name << "\"\n";
    }
    out << "source=" << route.source.number << ':' << static_cast<unsigned>(route.source.generation)
        << "\n";
    out << "destination=" << route.destination.number << ':'
        << static_cast<unsigned>(route.destination.generation) << "\n";
    out << "family=" << route.family_id << "\n";
    out << "payload_hex=" << hex_encode(route.payload) << "\n";
  }
  return out.str();
}

Result<GatewayRoute> apply_route_policy_entry(Gateway& gateway,
                                              const RoutePolicyEntry& entry) {
  return gateway.create_route(entry.source, entry.destination, entry.family_id);
}

Result<RoutePolicyEntry> find_route_policy_source(const RoutePolicyDocument& document,
                                                  ChannelId source) {
  const auto it = std::find_if(document.routes.begin(), document.routes.end(),
                               [source](const RoutePolicyEntry& entry) {
                                 return entry.source == source;
                               });
  if (it == document.routes.end()) {
    return make_error(ErrorScope::connection, ErrorCode::resource_limit,
                      CloseAction::none, "route policy source was not found");
  }
  return *it;
}

}  // namespace lattice
