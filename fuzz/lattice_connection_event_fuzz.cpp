#include "lattice/connection.hpp"

#include <fstream>
#include <iterator>

namespace {

lattice::PluginRegistry make_registry() {
  lattice::PluginRegistry registry;
  (void)registry.register_factory(lattice::EchoPlugin().descriptor(), [] {
    return std::make_unique<lattice::EchoPlugin>();
  });
  return registry;
}

}  // namespace

int main(int argc, char** argv) {
  lattice::Bytes input;
  if (argc == 2) {
    std::ifstream in(argv[1], std::ios::binary);
    input.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  lattice::ConnectionEngine left(lattice::LocalPolicy{}, make_registry());
  lattice::ConnectionEngine right(lattice::LocalPolicy{}, make_registry());
  auto left_hello = left.start();
  auto right_hello = right.start();
  if (!left_hello || !right_hello) {
    return 1;
  }
  (void)right.receive(left_hello.value()[0], false);
  (void)left.receive(right_hello.value()[0], false);
  for (std::uint8_t op : input) {
    if (op % 5U == 0U) {
      auto opened = left.open_channel(lattice::OpenRequest{7U, 1024U});
      if (opened) {
        (void)right.receive(opened.value().second[0], false);
      }
    } else {
      (void)right.receive(lattice::Bytes{op}, false);
    }
  }
  return 0;
}
