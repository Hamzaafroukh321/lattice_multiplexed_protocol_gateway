#include "lattice/frame.hpp"

#include <chrono>
#include <iostream>

int main() {
  lattice::FrameCodec encoder;
  lattice::Frame frame;
  frame.type = lattice::FrameType::data;
  frame.channel = lattice::ChannelId{1U, 1U};
  frame.frame_seq = 1U;
  frame.payload = lattice::Bytes(1024U, 0xABU);
  auto encoded = encoder.encode(frame);
  if (!encoded) {
    std::cerr << encoded.error().stable_code() << ": " << encoded.error().detail << '\n';
    return 1;
  }

  constexpr std::size_t iterations = 20000U;
  lattice::Bytes stream;
  stream.reserve(encoded.value().size() * iterations);
  for (std::size_t i = 0; i < iterations; ++i) {
    stream.insert(stream.end(), encoded.value().begin(), encoded.value().end());
  }

  const auto start = std::chrono::steady_clock::now();
  lattice::FrameCodec decoder;
  auto events = decoder.feed(stream, false);
  std::size_t decoded = 0;
  for (const auto& event : events) {
    if (event.status == lattice::DecodeStatus::frame) {
      decoded += event.frame->payload.size();
    }
    if (event.status == lattice::DecodeStatus::error) {
      std::cerr << event.error->stable_code() << ": " << event.error->detail << '\n';
      return 1;
    }
  }
  const auto end = std::chrono::steady_clock::now();
  const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  const double seconds = static_cast<double>(elapsed_ns) / 1'000'000'000.0;
  const double mib = static_cast<double>(decoded) / (1024.0 * 1024.0);
  std::cout << "frames=" << iterations << "\n";
  std::cout << "decoded_mib=" << mib << "\n";
  std::cout << "seconds=" << seconds << "\n";
  std::cout << "mib_per_second=" << (seconds == 0.0 ? 0.0 : mib / seconds) << "\n";
  return 0;
}
