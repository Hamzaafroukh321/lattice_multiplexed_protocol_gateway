#include "lattice/frame.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
  lattice::Bytes input;
  if (argc == 2) {
    std::ifstream in(argv[1], std::ios::binary);
    input.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  } else {
    lattice::Frame frame;
    frame.type = lattice::FrameType::hello;
    frame.frame_seq = 1U;
    frame.payload = lattice::Bytes{'f', 'u', 'z', 'z'};
    lattice::FrameCodec codec;
    auto encoded = codec.encode(frame);
    if (encoded) {
      input = encoded.value();
    }
  }
  lattice::FrameCodec decoder;
  auto events = decoder.feed(input, true);
  for (const auto& event : events) {
    if (event.status == lattice::DecodeStatus::frame) {
      lattice::FrameCodec encoder;
      auto roundtrip = encoder.encode(event.frame.value());
      if (!roundtrip) {
        std::cerr << roundtrip.error().stable_code() << '\n';
        return 1;
      }
    }
  }
  return 0;
}
