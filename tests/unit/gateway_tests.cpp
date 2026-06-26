#include "test_support.hpp"

#include "lattice/gateway.hpp"

using namespace lattice;

static CapabilitySet caps(std::size_t max_message = kDefaultMaxMessage) {
  CapabilitySet set;
  set.max_message = max_message;
  set.plugins.push_back(EchoPlugin().descriptor());
  return set;
}

static void GatewayCreatesRouteAndTransformsPayload() {
  Gateway gateway;
  REQUIRE_OK(gateway.add_translator(7U, [](std::span<const std::uint8_t> payload) -> Result<Bytes> {
    Bytes out{'x', ':'};
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
  }));
  auto route = gateway.create_route(ChannelId{1U, 1U}, ChannelId{2U, 1U}, 7U);
  REQUIRE_OK(route);
  auto forwarded = gateway.forward(caps(), caps(), route.value(), Bytes{'o', 'k'});
  REQUIRE_OK(forwarded);
  CHECK(forwarded.value() == Bytes({'x', ':', 'o', 'k'}));
}

static void GatewayTranslationRevalidatesLimit() {
  Gateway gateway;
  REQUIRE_OK(gateway.add_translator(7U, [](std::span<const std::uint8_t> payload) -> Result<Bytes> {
    Bytes out(payload.begin(), payload.end());
    out.push_back('!');
    return out;
  }));
  auto route = gateway.create_route(ChannelId{1U, 1U}, ChannelId{2U, 1U}, 7U);
  REQUIRE_OK(route);
  auto rejected = gateway.forward(caps(), caps(2U), route.value(), Bytes{'a', 'b'});
  CHECK(!rejected);
  CHECK(rejected.error().code == ErrorCode::resource_limit);
}

static void OpaqueForwardRequiresSameSchema() {
  Gateway gateway;
  CapabilitySet from = caps();
  CapabilitySet to = caps();
  to.plugins[0].schema_hash ^= 1U;
  auto rejected = gateway.translate(from, to, 7U, Bytes{'x'});
  CHECK(!rejected);
  CHECK(rejected.error().code == ErrorCode::plugin_decode);
}

void register_gateway_tests() {
  add_test("GatewayCreatesRouteAndTransformsPayload", &GatewayCreatesRouteAndTransformsPayload);
  add_test("GatewayTranslationRevalidatesLimit", &GatewayTranslationRevalidatesLimit);
  add_test("OpaqueForwardRequiresSameSchema", &OpaqueForwardRequiresSameSchema);
}
