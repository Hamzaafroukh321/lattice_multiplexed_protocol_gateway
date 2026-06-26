#include "test_support.hpp"

#include "lattice/plugin.hpp"

using namespace lattice;

static void PluginUnloadWaitsForQuiescence() {
  PluginRegistry registry = make_registry();
  auto lease = registry.create_lease(7U);
  REQUIRE_OK(lease);
  auto blocked = registry.unregister_family(7U);
  CHECK(!blocked);
  CHECK(blocked.error().code == ErrorCode::would_block);
  lease = PluginLease{};
  REQUIRE_OK(registry.unregister_family(7U));
  auto unavailable = registry.create_lease(7U);
  CHECK(!unavailable);
  CHECK(unavailable.error().code == ErrorCode::plugin_decode);
}

void register_plugin_tests() {
  add_test("PluginUnloadWaitsForQuiescence", &PluginUnloadWaitsForQuiescence);
}
