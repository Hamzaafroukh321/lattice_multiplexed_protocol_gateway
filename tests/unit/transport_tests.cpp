#include "test_support.hpp"

#include "lattice/transport.hpp"

#include <array>
#include <cstdint>

using namespace lattice;

static void UnixTransportPairRoundTripOrPortableError() {
#ifdef _WIN32
  auto pair = UnixTransport::pair_for_test();
  CHECK(!pair);
  CHECK(pair.error().scope == ErrorScope::transport);
  CHECK(pair.error().code == ErrorCode::transport_error);
#else
  auto pair = UnixTransport::pair_for_test();
  REQUIRE_OK(pair);
  auto transports = pair.take_value();
  const std::array<std::uint8_t, 4> payload{'l', 't', 'x', '1'};
  REQUIRE_OK(transports.first.write(payload));
  auto received = transports.second.read_some(payload.size());
  REQUIRE_OK(received);
  CHECK(received.value().size() == payload.size());
  CHECK(received.value()[0] == payload[0]);
  CHECK(received.value()[1] == payload[1]);
  CHECK(received.value()[2] == payload[2]);
  CHECK(received.value()[3] == payload[3]);
#endif
}

void register_transport_tests() {
  add_test("UnixTransportPairRoundTripOrPortableError",
           &UnixTransportPairRoundTripOrPortableError);
}
