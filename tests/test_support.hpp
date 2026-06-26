#pragma once

#include "lattice/connection.hpp"

#include <stdexcept>
#include <string>

void add_test(const char* name, void (*fn)());

#define CHECK(expr)                                                                  \
  do {                                                                               \
    if (!(expr)) {                                                                   \
      throw std::runtime_error(std::string("CHECK failed: ") + #expr);              \
    }                                                                                \
  } while (false)

#define REQUIRE_OK(result)                                                           \
  do {                                                                               \
    if (!(result)) {                                                                 \
      throw std::runtime_error(std::string("unexpected error: ") +                  \
                               (result).error().stable_code() + " " +              \
                               (result).error().detail);                             \
    }                                                                                \
  } while (false)

inline lattice::PluginRegistry make_registry() {
  lattice::PluginRegistry registry;
  auto descriptor = lattice::EchoPlugin().descriptor();
  auto reg = registry.register_factory(descriptor, [] {
    return std::make_unique<lattice::EchoPlugin>();
  });
  REQUIRE_OK(reg);
  return registry;
}
