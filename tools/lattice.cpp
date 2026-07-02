#include "lattice/connection.hpp"
#include "lattice/gateway.hpp"
#include "lattice/replay_store.hpp"
#include "lattice/trace.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

lattice::PluginRegistry registry() {
  lattice::PluginRegistry out;
  auto descriptor = lattice::EchoPlugin().descriptor();
  auto result = out.register_factory(descriptor, [] {
    return std::make_unique<lattice::EchoPlugin>();
  });
  if (!result) {
    throw std::runtime_error(result.error().detail);
  }
  return out;
}

struct BridgePolicy {
  lattice::ChannelId source{1U, 1U};
  lattice::ChannelId destination{2U, 1U};
  std::uint32_t family_id{7U};
  lattice::Bytes payload{'o', 'k'};
};

struct UnixBridgeArgs {
  std::string left;
  std::string right;
  std::string policy;
};

int unix_failure_exit() {
#ifdef _WIN32
  return 6;
#else
  return 3;
#endif
}

int print_error(const lattice::Error& error, int exit_code = 3) {
  std::cerr << error.stable_code() << ": " << error.detail << '\n';
  return exit_code;
}

lattice::Result<void> snapshot_error(const std::string& detail) {
  return lattice::make_error(lattice::ErrorScope::internal, lattice::ErrorCode::resource_limit,
                             lattice::CloseAction::none, detail);
}

std::string trim(std::string text) {
  const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
  return text;
}

std::string unquote(std::string text) {
  text = trim(std::move(text));
  if (text.size() >= 2U && text.front() == '"' && text.back() == '"') {
    return text.substr(1U, text.size() - 2U);
  }
  return text;
}

lattice::Result<std::uint32_t> parse_decimal_u32(const std::string& text) {
  if (text.empty()) {
    return lattice::make_error(lattice::ErrorScope::connection, lattice::ErrorCode::resource_limit,
                               lattice::CloseAction::none, "policy integer is empty");
  }
  std::uint64_t value = 0;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return lattice::make_error(lattice::ErrorScope::connection, lattice::ErrorCode::resource_limit,
                                 lattice::CloseAction::none, "policy integer is not decimal");
    }
    value = value * 10U + static_cast<std::uint64_t>(ch - '0');
    if (value > 0xFFFFFFFFULL) {
      return lattice::make_error(lattice::ErrorScope::connection, lattice::ErrorCode::resource_limit,
                                 lattice::CloseAction::none, "policy integer overflows u32");
    }
  }
  return static_cast<std::uint32_t>(value);
}

lattice::Result<lattice::ChannelId> parse_channel_id(std::string text) {
  text = unquote(std::move(text));
  const std::size_t colon = text.find(':');
  if (colon == std::string::npos) {
    return lattice::make_error(lattice::ErrorScope::channel, lattice::ErrorCode::resource_limit,
                               lattice::CloseAction::none, "policy channel must use number:generation");
  }
  auto number = parse_decimal_u32(text.substr(0U, colon));
  if (!number) {
    return number.error();
  }
  auto generation = parse_decimal_u32(text.substr(colon + 1U));
  if (!generation) {
    return generation.error();
  }
  if (generation.value() > 0xFFU) {
    return lattice::make_error(lattice::ErrorScope::channel, lattice::ErrorCode::resource_limit,
                               lattice::CloseAction::none, "policy channel generation exceeds u8");
  }
  return lattice::ChannelId{number.value(), static_cast<std::uint8_t>(generation.value())};
}

lattice::Result<lattice::Bytes> parse_hex_bytes(std::string text) {
  text = unquote(std::move(text));
  if ((text.size() % 2U) != 0U) {
    return lattice::make_error(lattice::ErrorScope::message, lattice::ErrorCode::resource_limit,
                               lattice::CloseAction::none, "policy payload hex has odd length");
  }
  lattice::Bytes out;
  out.reserve(text.size() / 2U);
  const auto nibble = [](char ch) -> int {
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
  for (std::size_t i = 0; i < text.size(); i += 2U) {
    const int high = nibble(text[i]);
    const int low = nibble(text[i + 1U]);
    if (high < 0 || low < 0) {
      return lattice::make_error(lattice::ErrorScope::message, lattice::ErrorCode::resource_limit,
                                 lattice::CloseAction::none, "policy payload contains non-hex byte");
    }
    out.push_back(static_cast<std::uint8_t>((high << 4) | low));
  }
  return out;
}

lattice::Result<BridgePolicy> read_bridge_policy(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return lattice::make_error(lattice::ErrorScope::connection, lattice::ErrorCode::resource_limit,
                               lattice::CloseAction::none, "cannot open bridge policy file");
  }
  BridgePolicy policy;
  std::string line;
  while (std::getline(in, line)) {
    const std::size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line = line.substr(0U, comment);
    }
    line = trim(std::move(line));
    if (line.empty() || line == "[route]") {
      continue;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
      return lattice::make_error(lattice::ErrorScope::connection, lattice::ErrorCode::resource_limit,
                                 lattice::CloseAction::none, "malformed bridge policy line");
    }
    const std::string key = trim(line.substr(0U, equals));
    const std::string value = trim(line.substr(equals + 1U));
    if (key == "source") {
      auto parsed = parse_channel_id(value);
      if (!parsed) {
        return parsed.error();
      }
      policy.source = parsed.value();
    } else if (key == "destination") {
      auto parsed = parse_channel_id(value);
      if (!parsed) {
        return parsed.error();
      }
      policy.destination = parsed.value();
    } else if (key == "family") {
      auto parsed = parse_decimal_u32(unquote(value));
      if (!parsed) {
        return parsed.error();
      }
      policy.family_id = parsed.value();
    } else if (key == "payload_hex") {
      auto parsed = parse_hex_bytes(value);
      if (!parsed) {
        return parsed.error();
      }
      policy.payload = parsed.value();
    } else {
      return lattice::make_error(lattice::ErrorScope::connection, lattice::ErrorCode::resource_limit,
                                 lattice::CloseAction::none, "unknown bridge policy key");
    }
  }
  return policy;
}

lattice::Result<void> write_all(lattice::UnixTransport& transport,
                                const std::vector<lattice::Bytes>& chunks) {
  for (const auto& chunk : chunks) {
    auto written = transport.write(chunk);
    if (!written) {
      return written.error();
    }
  }
  return {};
}

lattice::Result<void> negotiate_unix_peer(lattice::UnixTransport& transport,
                                          lattice::ConnectionEngine& engine) {
  auto hello = engine.start();
  if (!hello) {
    return hello.error();
  }
  auto written = write_all(transport, hello.value());
  if (!written) {
    return written.error();
  }

  while (engine.state() == lattice::ConnectionState::negotiating) {
    auto read = transport.read_some(lattice::kDefaultMaxFrame);
    if (!read) {
      return read.error();
    }
    if (read.value().empty()) {
      return lattice::make_error(lattice::ErrorScope::transport,
                                 lattice::ErrorCode::transport_error,
                                 lattice::CloseAction::close_connection,
                                 "peer closed before HELLO negotiation completed");
    }
    auto produced = engine.receive(read.value(), false);
    if (!produced) {
      return produced.error();
    }
    auto out = write_all(transport, produced.value());
    if (!out) {
      return out.error();
    }
  }
  return {};
}

lattice::Result<void> load_snapshot_if_present(lattice::ConnectionEngine& engine,
                                               const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::error_code exists_error;
  const bool exists = std::filesystem::exists(path, exists_error);
  if (exists_error) {
    return snapshot_error("cannot inspect replay snapshot path");
  }
  if (!exists) {
    return {};
  }
  auto text = lattice::ReplaySnapshotStore::load_text(path);
  if (!text) {
    return text.error();
  }
  return engine.load_replay_snapshot(text.value());
}

lattice::Result<void> save_snapshot(const lattice::ConnectionEngine& engine,
                                    const std::string& path) {
  if (path.empty()) {
    return {};
  }
  auto text = engine.export_replay_snapshot();
  if (!text) {
    return text.error();
  }
  return lattice::ReplaySnapshotStore::save_text(path, text.value());
}

void print_capabilities(const char* prefix, const lattice::CapabilitySet& caps) {
  std::cout << prefix << " LTX/" << caps.major
            << " max_frame=" << caps.max_frame
            << " max_message=" << caps.max_message
            << " channels=" << caps.max_channels
            << " plugins=" << caps.plugins.size() << '\n';
}

std::optional<UnixBridgeArgs> parse_unix_bridge_args(int argc, char** argv) {
  UnixBridgeArgs args;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string key = argv[i];
    if (key == "--left") {
      args.left = argv[i + 1];
    } else if (key == "--right") {
      args.right = argv[i + 1];
    } else if (key == "--policy") {
      args.policy = argv[i + 1];
    } else {
      return std::nullopt;
    }
  }
  if ((argc % 2) != 0 || args.left.empty() || args.right.empty() || args.policy.empty()) {
    return std::nullopt;
  }
  return args;
}

int dump_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "lattice: cannot open " << path << '\n';
    return 3;
  }
  lattice::Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  lattice::FrameCodec codec;
  auto events = codec.feed(bytes, true);
  for (const auto& event : events) {
    if (event.status == lattice::DecodeStatus::frame) {
      std::cout << "frame type=" << static_cast<unsigned>(event.frame->type)
                << " channel=" << event.frame->channel.str()
                << " seq=" << event.frame->frame_seq
                << " payload=" << event.frame->payload.size() << '\n';
    } else if (event.status == lattice::DecodeStatus::error) {
      std::cerr << event.error->stable_code() << ": " << event.error->detail << '\n';
      return 3;
    }
  }
  return 0;
}

int replay_trace(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "lattice: cannot open " << path << '\n';
    return 3;
  }
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto parsed = lattice::TraceLog::parse(text);
  if (!parsed) {
    std::cerr << parsed.error().stable_code() << ": " << parsed.error().detail << '\n';
    return 3;
  }
  auto report = lattice::verify_trace_replay(parsed.value());
  if (!report) {
    std::cerr << report.error().stable_code() << ": " << report.error().detail << '\n';
    return 3;
  }
  std::cout << "events=" << report.value().total_events
            << " transport=" << report.value().transport_events
            << " api=" << report.value().api_events
            << " timer=" << report.value().timer_events
            << " plugin=" << report.value().plugin_events
            << " diagnostic=" << report.value().diagnostic_events
            << " bytes=" << report.value().total_bytes
            << " canonical=" << (report.value().canonical ? "yes" : "no") << '\n';
  return 0;
}

int probe_memory(const std::string& snapshot_path = {}) {
  lattice::ConnectionEngine left(lattice::LocalPolicy{}, registry());
  lattice::ConnectionEngine right(lattice::LocalPolicy{}, registry());
  auto loaded = load_snapshot_if_present(left, snapshot_path);
  if (!loaded) {
    return print_error(loaded.error());
  }
  auto left_hello = left.start();
  auto right_hello = right.start();
  if (!left_hello || !right_hello) {
    std::cerr << "lattice: failed to emit HELLO\n";
    return 10;
  }
  auto right_result = right.receive(left_hello.value()[0], false);
  auto left_result = left.receive(right_hello.value()[0], false);
  if (!right_result || !left_result || !left.capabilities() || !right.capabilities()) {
    std::cerr << "lattice: negotiation failed\n";
    return 3;
  }
  std::cout << "LTX/" << left.capabilities()->major
            << " max_frame=" << left.capabilities()->max_frame
            << " max_message=" << left.capabilities()->max_message
            << " channels=" << left.capabilities()->max_channels
            << " plugins=" << left.capabilities()->plugins.size() << '\n';
  auto saved = save_snapshot(left, snapshot_path);
  if (!saved) {
    return print_error(saved.error());
  }
  return 0;
}

int probe_unix(const std::string& path, const std::string& snapshot_path = {}) {
  auto transport = lattice::UnixTransport::connect_path(path);
  if (!transport) {
    return print_error(transport.error(), unix_failure_exit());
  }
  lattice::ConnectionEngine engine(lattice::LocalPolicy{}, registry());
  auto loaded = load_snapshot_if_present(engine, snapshot_path);
  if (!loaded) {
    return print_error(loaded.error());
  }
  auto negotiated = negotiate_unix_peer(transport.value(), engine);
  if (!negotiated) {
    return print_error(negotiated.error(), unix_failure_exit());
  }
  if (!engine.capabilities()) {
    std::cerr << "lattice: Unix probe did not negotiate capabilities\n";
    return 10;
  }
  print_capabilities("transport=unix", engine.capabilities().value());
  auto saved = save_snapshot(engine, snapshot_path);
  if (!saved) {
    return print_error(saved.error());
  }
  return 0;
}

int serve_unix(const std::string& path, const std::string& plugin,
               const std::string& snapshot_path = {}) {
  if (plugin != "echo") {
    std::cerr << "lattice: unsupported plugin '" << plugin << "'\n";
    return 2;
  }
  auto listener = lattice::UnixListener::bind_path(path);
  if (!listener) {
    return print_error(listener.error(), unix_failure_exit());
  }
  auto accepted = listener.value().accept_one();
  if (!accepted) {
    return print_error(accepted.error(), unix_failure_exit());
  }
  lattice::ConnectionEngine engine(lattice::LocalPolicy{}, registry());
  auto loaded = load_snapshot_if_present(engine, snapshot_path);
  if (!loaded) {
    return print_error(loaded.error());
  }
  auto negotiated = negotiate_unix_peer(accepted.value(), engine);
  if (!negotiated) {
    return print_error(negotiated.error(), unix_failure_exit());
  }
  if (!engine.capabilities()) {
    std::cerr << "lattice: Unix endpoint did not negotiate capabilities\n";
    return 10;
  }
  print_capabilities("serve=unix", engine.capabilities().value());
  auto saved = save_snapshot(engine, snapshot_path);
  if (!saved) {
    return print_error(saved.error());
  }
  return 0;
}

int fixture_memory_hello() {
  lattice::ConnectionEngine left(lattice::LocalPolicy{}, registry());
  lattice::ConnectionEngine right(lattice::LocalPolicy{}, registry());
  auto left_hello = left.start();
  auto right_hello = right.start();
  if (!left_hello || !right_hello) {
    std::cerr << "lattice: failed to emit fixture HELLO\n";
    return 10;
  }
  lattice::TraceLog log;
  log.record(lattice::TraceEvent{0U, lattice::TraceKind::transport_bytes, "left.hello",
                                 left_hello.value()[0]});
  log.record(lattice::TraceEvent{0U, lattice::TraceKind::transport_bytes, "right.hello",
                                 right_hello.value()[0]});
  auto serialized = log.serialize();
  if (!serialized) {
    std::cerr << serialized.error().stable_code() << ": " << serialized.error().detail << '\n';
    return 3;
  }
  std::cout << serialized.value();
  return 0;
}

int bridge_memory(const BridgePolicy& policy) {
  lattice::ConnectionEngine left(lattice::LocalPolicy{}, registry());
  lattice::ConnectionEngine right(lattice::LocalPolicy{}, registry());
  auto left_hello = left.start();
  auto right_hello = right.start();
  if (!left_hello || !right_hello) {
    std::cerr << "lattice: failed to emit bridge HELLO\n";
    return 10;
  }
  auto right_result = right.receive(left_hello.value()[0], false);
  auto left_result = left.receive(right_hello.value()[0], false);
  if (!right_result || !left_result || !left.capabilities() || !right.capabilities()) {
    std::cerr << "lattice: bridge negotiation failed\n";
    return 3;
  }
  lattice::Gateway gateway;
  auto route = gateway.create_route(policy.source, policy.destination, policy.family_id);
  if (!route) {
    std::cerr << route.error().stable_code() << ": " << route.error().detail << '\n';
    return 3;
  }
  auto bridged = gateway.bridge_message(left.capabilities().value(), right.capabilities().value(),
                                        policy.source, policy.payload);
  if (!bridged) {
    std::cerr << bridged.error().stable_code() << ": " << bridged.error().detail << '\n';
    return 3;
  }
  std::cout << "bridge=memory route=" << bridged.value().route_id
            << " source=" << bridged.value().source.str()
            << " destination=" << bridged.value().destination.str()
            << " family=" << bridged.value().family_id
            << " bytes=" << bridged.value().payload.size() << '\n';
  return 0;
}

int bridge_unix(const UnixBridgeArgs& args) {
  auto policy = read_bridge_policy(args.policy);
  if (!policy) {
    return print_error(policy.error());
  }
  auto left_transport = lattice::UnixTransport::connect_path(args.left);
  if (!left_transport) {
    return print_error(left_transport.error(), unix_failure_exit());
  }
  auto right_transport = lattice::UnixTransport::connect_path(args.right);
  if (!right_transport) {
    return print_error(right_transport.error(), unix_failure_exit());
  }

  lattice::ConnectionEngine left(lattice::LocalPolicy{}, registry());
  lattice::ConnectionEngine right(lattice::LocalPolicy{}, registry());
  auto left_negotiated = negotiate_unix_peer(left_transport.value(), left);
  if (!left_negotiated) {
    return print_error(left_negotiated.error(), unix_failure_exit());
  }
  auto right_negotiated = negotiate_unix_peer(right_transport.value(), right);
  if (!right_negotiated) {
    return print_error(right_negotiated.error(), unix_failure_exit());
  }
  if (!left.capabilities() || !right.capabilities()) {
    std::cerr << "lattice: Unix bridge did not negotiate both endpoints\n";
    return 10;
  }

  lattice::Gateway gateway;
  auto route = gateway.create_route(policy.value().source, policy.value().destination,
                                    policy.value().family_id);
  if (!route) {
    return print_error(route.error());
  }
  auto bridged = gateway.bridge_message(left.capabilities().value(), right.capabilities().value(),
                                        policy.value().source, policy.value().payload);
  if (!bridged) {
    return print_error(bridged.error());
  }
  std::cout << "bridge=unix route=" << bridged.value().route_id
            << " left=" << args.left
            << " right=" << args.right
            << " source=" << bridged.value().source.str()
            << " destination=" << bridged.value().destination.str()
            << " family=" << bridged.value().family_id
            << " bytes=" << bridged.value().payload.size() << '\n';
  return 0;
}

void usage() {
  std::cout << "usage:\n"
            << "  lattice probe --memory\n"
            << "  lattice probe --memory [--snapshot <file>]\n"
            << "  lattice probe --socket <path> [--snapshot <file>]\n"
            << "  lattice serve --socket <path> --plugin echo [--snapshot <file>]\n"
            << "  lattice bridge --memory [--policy <file>]\n"
            << "  lattice bridge --left <path> --right <path> --policy <file>\n"
            << "  lattice dump <file>\n"
            << "  lattice replay <trace-file>\n"
            << "  lattice fixture --memory-hello\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string command = argv[1];
  if (command == "probe" && argc == 3 && std::string(argv[2]) == "--memory") {
    return probe_memory();
  }
  if (command == "probe" && argc == 5 && std::string(argv[2]) == "--memory" &&
      std::string(argv[3]) == "--snapshot") {
    return probe_memory(argv[4]);
  }
  if (command == "probe" && argc == 4 && std::string(argv[2]) == "--socket") {
    return probe_unix(argv[3]);
  }
  if (command == "probe" && argc == 6 && std::string(argv[2]) == "--socket" &&
      std::string(argv[4]) == "--snapshot") {
    return probe_unix(argv[3], argv[5]);
  }
  if (command == "serve" && argc == 6 && std::string(argv[2]) == "--socket" &&
      std::string(argv[4]) == "--plugin") {
    return serve_unix(argv[3], argv[5]);
  }
  if (command == "serve" && argc == 8 && std::string(argv[2]) == "--socket" &&
      std::string(argv[4]) == "--plugin" && std::string(argv[6]) == "--snapshot") {
    return serve_unix(argv[3], argv[5], argv[7]);
  }
  if (command == "bridge" && argc == 3 && std::string(argv[2]) == "--memory") {
    return bridge_memory(BridgePolicy{});
  }
  if (command == "bridge" && argc == 5 && std::string(argv[2]) == "--memory" &&
      std::string(argv[3]) == "--policy") {
    auto policy = read_bridge_policy(argv[4]);
    if (!policy) {
      std::cerr << policy.error().stable_code() << ": " << policy.error().detail << '\n';
      return 3;
    }
    return bridge_memory(policy.value());
  }
  if (command == "bridge") {
    auto args = parse_unix_bridge_args(argc, argv);
    if (args.has_value()) {
      return bridge_unix(args.value());
    }
  }
  if (command == "dump" && argc == 3) {
    return dump_file(argv[2]);
  }
  if (command == "replay" && argc == 3) {
    return replay_trace(argv[2]);
  }
  if (command == "fixture" && argc == 3 && std::string(argv[2]) == "--memory-hello") {
    return fixture_memory_hello();
  }
  usage();
  return 2;
}
