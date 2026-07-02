#include "test_support.hpp"

#include "lattice/route_policy.hpp"

using namespace lattice;

static void RoutePolicyParsesMultipleRoutes() {
  const std::string text =
      "[route]\n"
      "name=\"primary\"\n"
      "source=1:1\n"
      "destination=2:1\n"
      "family=7\n"
      "payload_hex=6f6b\n"
      "\n"
      "[route]\n"
      "name=\"backup\"\n"
      "source=3:1\n"
      "destination=4:1\n"
      "family=7\n"
      "payload_hex=626b\n";

  auto parsed = parse_route_policy_text(text);
  REQUIRE_OK(parsed);
  CHECK(parsed.value().routes.size() == 2U);
  CHECK(parsed.value().routes[0].name == "primary");
  CHECK(parsed.value().routes[0].payload == Bytes({'o', 'k'}));
  CHECK((parsed.value().routes[1].source == ChannelId{3U, 1U}));
}

static void RoutePolicyRejectsDuplicateSources() {
  const std::string text =
      "[route]\n"
      "source=1:1\n"
      "destination=2:1\n"
      "family=7\n"
      "[route]\n"
      "source=1:1\n"
      "destination=3:1\n"
      "family=7\n";

  auto parsed = parse_route_policy_text(text);
  CHECK(!parsed);
  CHECK(parsed.error().code == ErrorCode::resource_limit);
}

static void RoutePolicyRejectsMalformedHex() {
  const std::string text =
      "[route]\n"
      "source=1:1\n"
      "destination=2:1\n"
      "family=7\n"
      "payload_hex=abc\n";

  auto parsed = parse_route_policy_text(text);
  CHECK(!parsed);
  CHECK(parsed.error().code == ErrorCode::malformed_tlv);
}

static void RoutePolicySerializesCanonicalText() {
  RoutePolicyDocument document;
  document.routes.push_back(RoutePolicyEntry{"primary", ChannelId{1U, 1U},
                                             ChannelId{2U, 1U}, 7U,
                                             Bytes{'o', 'k'}});

  auto serialized = serialize_route_policy(document);
  REQUIRE_OK(serialized);
  CHECK(serialized.value().find("name=\"primary\"") != std::string::npos);
  CHECK(serialized.value().find("payload_hex=6f6b") != std::string::npos);
  auto reparsed = parse_route_policy_text(serialized.value());
  REQUIRE_OK(reparsed);
  CHECK(reparsed.value().routes[0].payload == Bytes({'o', 'k'}));
}

static void RoutePolicyAppliesGatewayRoute() {
  RoutePolicyEntry entry;
  entry.source = ChannelId{9U, 1U};
  entry.destination = ChannelId{10U, 1U};
  entry.family_id = 7U;
  entry.payload = Bytes{'x'};

  Gateway gateway;
  auto route = apply_route_policy_entry(gateway, entry);
  REQUIRE_OK(route);
  CHECK(route.value().source == entry.source);
  auto found = gateway.find_route(entry.source);
  REQUIRE_OK(found);
  CHECK(found.value().destination == entry.destination);
}

static void RoutePolicyFindsSource() {
  RoutePolicyDocument document;
  document.routes.push_back(RoutePolicyEntry{"one", ChannelId{1U, 1U},
                                             ChannelId{2U, 1U}, 7U, Bytes{'a'}});
  document.routes.push_back(RoutePolicyEntry{"two", ChannelId{3U, 1U},
                                             ChannelId{4U, 1U}, 7U, Bytes{'b'}});

  auto found = find_route_policy_source(document, ChannelId{3U, 1U});
  REQUIRE_OK(found);
  CHECK(found.value().name == "two");
  auto missing = find_route_policy_source(document, ChannelId{5U, 1U});
  CHECK(!missing);
}

void register_route_policy_tests() {
  add_test("RoutePolicyParsesMultipleRoutes", &RoutePolicyParsesMultipleRoutes);
  add_test("RoutePolicyRejectsDuplicateSources", &RoutePolicyRejectsDuplicateSources);
  add_test("RoutePolicyRejectsMalformedHex", &RoutePolicyRejectsMalformedHex);
  add_test("RoutePolicySerializesCanonicalText", &RoutePolicySerializesCanonicalText);
  add_test("RoutePolicyAppliesGatewayRoute", &RoutePolicyAppliesGatewayRoute);
  add_test("RoutePolicyFindsSource", &RoutePolicyFindsSource);
}
