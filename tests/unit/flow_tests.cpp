#include "test_support.hpp"

#include "lattice/channel.hpp"

using namespace lattice;

static void CreditConservationProperty() {
  FlowAccount account(100U);
  REQUIRE_OK(account.reserve(40U));
  CHECK(account.available() == 60U);
  CHECK(account.reserved() == 40U);
  REQUIRE_OK(account.release(40U));
  CHECK(account.available() == 100U);
  CHECK(account.reserved() == 0U);
  CHECK(account.invariant_holds());
}

static void CreditOverflowCloses() {
  FlowAccount account(100U);
  auto overflow = account.grant(1U);
  CHECK(!overflow);
  CHECK(overflow.error().code == ErrorCode::flow_overflow);
}

void register_flow_tests() {
  add_test("CreditConservationProperty", &CreditConservationProperty);
  add_test("CreditOverflowCloses", &CreditOverflowCloses);
}
