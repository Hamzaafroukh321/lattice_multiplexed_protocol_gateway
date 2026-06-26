#include "lattice/connection.hpp"
#include "lattice/trace.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

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

int probe_memory() {
  lattice::ConnectionEngine left(lattice::LocalPolicy{}, registry());
  lattice::ConnectionEngine right(lattice::LocalPolicy{}, registry());
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

void usage() {
  std::cout << "usage:\n"
            << "  lattice probe --memory\n"
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
  if (command == "dump" && argc == 3) {
    return dump_file(argv[2]);
  }
  if (command == "replay" && argc == 3) {
    return replay_trace(argv[2]);
  }
  if (command == "fixture" && argc == 3 && std::string(argv[2]) == "--memory-hello") {
    return fixture_memory_hello();
  }
  if (command == "serve" || command == "bridge") {
    std::cerr << "lattice: Unix-socket serve/bridge is not available in this portable build\n";
    return 6;
  }
  usage();
  return 2;
}
