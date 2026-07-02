#include "../deep_fuzz_support.hpp"

namespace {
int RunTarget(const std::uint8_t* data, std::size_t size) {
  lattice::fuzz::Input input(data, size);
  lattice::fuzz::run_connection_reset_goaway(input);
  return 0;
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  return RunTarget(data, size);
}

#ifndef LATTICE_LIBFUZZER
int main(int argc, char** argv) { return lattice::fuzz::run_standalone(argc, argv, &RunTarget); }
#endif
