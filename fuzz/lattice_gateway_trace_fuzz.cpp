#include "lattice/gateway.hpp"

#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
  lattice::Bytes input;
  if (argc == 2) {
    std::ifstream in(argv[1], std::ios::binary);
    input.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  } else {
    input = lattice::Bytes{'o', 'k'};
  }
  lattice::CapabilitySet from;
  lattice::CapabilitySet to;
  from.plugins.push_back(lattice::EchoPlugin().descriptor());
  to.plugins.push_back(lattice::EchoPlugin().descriptor());
  lattice::Gateway gateway;
  auto translated = gateway.translate(from, to, 7U, input);
  return translated ? 0 : 1;
}
