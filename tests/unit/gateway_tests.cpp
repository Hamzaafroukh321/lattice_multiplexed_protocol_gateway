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

static void TranslatorAllowsAsymmetricSchema() {
  Gateway gateway;
  REQUIRE_OK(gateway.add_translator(7U, [](std::span<const std::uint8_t> payload) -> Result<Bytes> {
    Bytes out{'t'};
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
  }));
  CapabilitySet from = caps();
  CapabilitySet to = caps();
  to.plugins[0].schema_hash ^= 1U;
  auto forwarded = gateway.translate(from, to, 7U, Bytes{'x'});
  REQUIRE_OK(forwarded);
  CHECK(forwarded.value() == Bytes({'t', 'x'}));
}

static void GatewayBridgesRegisteredRouteBySource() {
  Gateway gateway;
  REQUIRE_OK(gateway.add_translator(7U, [](std::span<const std::uint8_t> payload) -> Result<Bytes> {
    Bytes out(payload.begin(), payload.end());
    out.push_back('!');
    return out;
  }));
  auto route = gateway.create_route(ChannelId{9U, 2U}, ChannelId{10U, 1U}, 7U);
  REQUIRE_OK(route);
  auto bridged = gateway.bridge_message(caps(), caps(), ChannelId{9U, 2U}, Bytes{'o', 'k'});
  REQUIRE_OK(bridged);
  const ChannelId expected_source{9U, 2U};
  const ChannelId expected_destination{10U, 1U};
  CHECK(bridged.value().route_id == route.value().id);
  CHECK(bridged.value().source == expected_source);
  CHECK(bridged.value().destination == expected_destination);
  CHECK(bridged.value().payload == Bytes({'o', 'k', '!'}));
}

void register_gateway_tests() {
  add_test("GatewayCreatesRouteAndTransformsPayload", &GatewayCreatesRouteAndTransformsPayload);
  add_test("GatewayTranslationRevalidatesLimit", &GatewayTranslationRevalidatesLimit);
  add_test("OpaqueForwardRequiresSameSchema", &OpaqueForwardRequiresSameSchema);
  add_test("TranslatorAllowsAsymmetricSchema", &TranslatorAllowsAsymmetricSchema);
  add_test("GatewayBridgesRegisteredRouteBySource", &GatewayBridgesRegisteredRouteBySource);
}
