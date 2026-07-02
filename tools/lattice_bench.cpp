#include "lattice/connection.hpp"
#include "lattice/frame.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct DecodeMetrics {
  std::size_t frames{0};
  double decoded_mib{0.0};
  double seconds{0.0};
  double mib_per_second{0.0};
};

struct LatencyMetrics {
  std::size_t messages{0};
  double p50_us{0.0};
  double p99_us{0.0};
  double max_us{0.0};
};

struct FuzzMetrics {
  double frame_exec_per_second{0.0};
  double event_exec_per_second{0.0};
};

lattice::PluginRegistry make_registry() {
  lattice::PluginRegistry registry;
  (void)registry.register_factory(lattice::EchoPlugin().descriptor(), [] {
    return std::make_unique<lattice::EchoPlugin>();
  });
  return registry;
}

void print_error(const std::string& context, const lattice::Error& error) {
  std::cerr << context << ": " << error.stable_code() << ": " << error.detail << '\n';
}

bool handshake(lattice::ConnectionEngine& left, lattice::ConnectionEngine& right) {
  auto left_hello = left.start();
  auto right_hello = right.start();
  if (!left_hello) {
    print_error("left.start", left_hello.error());
    return false;
  }
  if (!right_hello) {
    print_error("right.start", right_hello.error());
    return false;
  }
  auto right_received = right.receive(left_hello.value()[0], false);
  if (!right_received) {
    print_error("right.receive(hello)", right_received.error());
    return false;
  }
  auto left_received = left.receive(right_hello.value()[0], false);
  if (!left_received) {
    print_error("left.receive(hello)", left_received.error());
    return false;
  }
  return left.state() == lattice::ConnectionState::active &&
         right.state() == lattice::ConnectionState::active;
}

std::optional<std::size_t> current_rss_bytes() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(counters.WorkingSetSize);
#else
  std::ifstream statm("/proc/self/statm");
  std::size_t total_pages = 0;
  std::size_t resident_pages = 0;
  statm >> total_pages >> resident_pages;
  (void)total_pages;
  if (!statm) {
    return std::nullopt;
  }
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    return std::nullopt;
  }
  return resident_pages * static_cast<std::size_t>(page_size);
#endif
}

DecodeMetrics measure_decode() {
  lattice::FrameCodec encoder;
  lattice::Frame frame;
  frame.type = lattice::FrameType::data;
  frame.channel = lattice::ChannelId{1U, 1U};
  frame.frame_seq = 1U;
  frame.payload = lattice::Bytes(1024U, 0xABU);
  auto encoded = encoder.encode(frame);
  if (!encoded) {
    print_error("encode(DATA)", encoded.error());
    return {};
  }

  constexpr std::size_t iterations = 20000U;
  lattice::Bytes stream;
  stream.reserve(encoded.value().size() * iterations);
  for (std::size_t i = 0; i < iterations; ++i) {
    stream.insert(stream.end(), encoded.value().begin(), encoded.value().end());
  }

  const auto start = Clock::now();
  lattice::FrameCodec decoder;
  auto events = decoder.feed(stream, false);
  std::size_t decoded = 0;
  for (const auto& event : events) {
    if (event.status == lattice::DecodeStatus::frame) {
      decoded += event.frame->payload.size();
    }
    if (event.status == lattice::DecodeStatus::error) {
      print_error("decode(DATA stream)", event.error.value());
      return {};
    }
  }
  const auto end = Clock::now();
  const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  const double seconds = static_cast<double>(elapsed_ns.count()) / 1'000'000'000.0;
  const double mib = static_cast<double>(decoded) / (1024.0 * 1024.0);
  return DecodeMetrics{iterations, mib, seconds, seconds == 0.0 ? 0.0 : mib / seconds};
}

LatencyMetrics measure_latency() {
  constexpr std::size_t iterations = 512U;
  lattice::DeterministicExecutor executor(iterations + 8U, 1U);
  lattice::ConnectionEngine left(lattice::LocalPolicy{}, make_registry());
  lattice::ConnectionEngine right(lattice::LocalPolicy{}, make_registry(), &executor);
  if (!handshake(left, right)) {
    return {};
  }

  auto opened = left.open_channel(lattice::OpenRequest{7U, lattice::kDefaultConnectionWindow});
  if (!opened) {
    print_error("open_channel", opened.error());
    return {};
  }
  auto accepted = right.receive(opened.value().second[0], false);
  if (!accepted) {
    print_error("receive(OPEN)", accepted.error());
    return {};
  }

  const lattice::Bytes payload(1024U, 0xCDU);
  std::vector<double> samples_us;
  samples_us.reserve(iterations);
  for (std::size_t i = 0; i < iterations; ++i) {
    const auto before_events = right.events().size();
    const auto start = Clock::now();
    auto sent = left.send(opened.value().first, payload);
    if (!sent) {
      print_error("send(DATA)", sent.error());
      return {};
    }
    for (const auto& chunk : sent.value()) {
      auto received = right.receive(chunk, false);
      if (!received) {
        print_error("receive(DATA)", received.error());
        return {};
      }
    }
    const auto end = Clock::now();

    bool delivered = false;
    for (std::size_t event_index = before_events; event_index < right.events().size();
         ++event_index) {
      const auto& event = right.events()[event_index];
      delivered = delivered ||
                  (event.kind == lattice::ConnectionEvent::Kind::message_delivered &&
                   event.payload == payload);
    }
    if (!delivered) {
      std::cerr << "message delivery was not observed during latency probe\n";
      return {};
    }

    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    samples_us.push_back(static_cast<double>(elapsed_ns.count()) / 1000.0);
  }

  std::sort(samples_us.begin(), samples_us.end());
  const std::size_t p50_index = samples_us.size() / 2U;
  const std::size_t p99_index = (samples_us.size() * 99U) / 100U;
  return LatencyMetrics{samples_us.size(), samples_us[p50_index],
                        samples_us[std::min(p99_index, samples_us.size() - 1U)],
                        samples_us.back()};
}

std::size_t measure_active_channels() {
  lattice::ConnectionEngine left(lattice::LocalPolicy{}, make_registry());
  lattice::ConnectionEngine right(lattice::LocalPolicy{}, make_registry());
  if (!handshake(left, right)) {
    return 0;
  }

  std::size_t opened_count = 0;
  for (std::size_t i = 0; i < lattice::kDefaultMaxChannels; ++i) {
    auto opened = left.open_channel(lattice::OpenRequest{7U, 1024U});
    if (!opened) {
      print_error("open_channel(capacity)", opened.error());
      return opened_count;
    }
    auto accepted = right.receive(opened.value().second[0], false);
    if (!accepted) {
      print_error("receive(OPEN capacity)", accepted.error());
      return opened_count;
    }
    ++opened_count;
  }
  return opened_count;
}

FuzzMetrics measure_fuzz_speed() {
  lattice::Frame frame;
  frame.type = lattice::FrameType::hello;
  frame.frame_seq = 1U;
  frame.payload = lattice::Bytes{'f', 'u', 'z', 'z'};
  lattice::FrameCodec codec;
  auto encoded = codec.encode(frame);
  if (!encoded) {
    print_error("encode(fuzz seed)", encoded.error());
    return {};
  }

  constexpr std::size_t frame_iterations = 100000U;
  std::size_t frame_events = 0;
  const auto frame_start = Clock::now();
  for (std::size_t i = 0; i < frame_iterations; ++i) {
    lattice::FrameCodec decoder;
    auto events = decoder.feed(encoded.value(), true);
    frame_events += events.size();
  }
  const auto frame_end = Clock::now();
  if (frame_events == 0U) {
    std::cerr << "frame fuzz speed probe produced no decoder events\n";
    return {};
  }

  constexpr std::size_t event_iterations = 10000U;
  const auto event_start = Clock::now();
  for (std::size_t i = 0; i < event_iterations; ++i) {
    lattice::ConnectionEngine left(lattice::LocalPolicy{}, make_registry());
    lattice::ConnectionEngine right(lattice::LocalPolicy{}, make_registry());
    if (!handshake(left, right)) {
      return {};
    }
    const std::uint8_t op = static_cast<std::uint8_t>(i & 0xFFU);
    (void)right.receive(lattice::Bytes{op}, false);
  }
  const auto event_end = Clock::now();

  const auto frame_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(frame_end - frame_start);
  const auto event_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(event_end - event_start);
  const double frame_seconds = static_cast<double>(frame_ns.count()) / 1'000'000'000.0;
  const double event_seconds = static_cast<double>(event_ns.count()) / 1'000'000'000.0;
  return FuzzMetrics{
      frame_seconds == 0.0 ? 0.0 : static_cast<double>(frame_iterations) / frame_seconds,
      event_seconds == 0.0 ? 0.0 : static_cast<double>(event_iterations) / event_seconds};
}

}  // namespace

int main() {
  const auto decode = measure_decode();
  if (decode.frames == 0U) {
    return 1;
  }
  const auto latency = measure_latency();
  if (latency.messages == 0U) {
    return 1;
  }
  const std::size_t active_channels = measure_active_channels();
  if (active_channels != lattice::kDefaultMaxChannels) {
    return 1;
  }
  const auto fuzz = measure_fuzz_speed();
  if (fuzz.frame_exec_per_second == 0.0 || fuzz.event_exec_per_second == 0.0) {
    return 1;
  }
  const auto rss = current_rss_bytes();

  std::cout << "frames=" << decode.frames << "\n";
  std::cout << "decoded_mib=" << decode.decoded_mib << "\n";
  std::cout << "seconds=" << decode.seconds << "\n";
  std::cout << "mib_per_second=" << decode.mib_per_second << "\n";
  std::cout << "latency_messages=" << latency.messages << "\n";
  std::cout << "latency_p50_us=" << latency.p50_us << "\n";
  std::cout << "latency_p99_us=" << latency.p99_us << "\n";
  std::cout << "latency_max_us=" << latency.max_us << "\n";
  std::cout << "default_max_frame_bytes=" << lattice::kDefaultMaxFrame << "\n";
  std::cout << "default_max_message_bytes=" << lattice::kDefaultMaxMessage << "\n";
  std::cout << "default_connection_window_bytes=" << lattice::kDefaultConnectionWindow << "\n";
  std::cout << "active_channels=" << active_channels << "\n";
  if (rss.has_value()) {
    std::cout << "process_rss_bytes=" << rss.value() << "\n";
    std::cout << "process_rss_mib=" << (static_cast<double>(rss.value()) / (1024.0 * 1024.0))
              << "\n";
  } else {
    std::cout << "process_rss_bytes=unavailable\n";
  }
  std::cout << "frame_fuzz_exec_per_second=" << fuzz.frame_exec_per_second << "\n";
  std::cout << "connection_event_exec_per_second=" << fuzz.event_exec_per_second << "\n";
  return 0;
}
