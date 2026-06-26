#pragma once

#include "lattice/connection.hpp"

namespace lattice {

class GatewayPolicy {
 public:
  [[nodiscard]] bool may_forward(const CapabilitySet& from, const CapabilitySet& to,
                                 std::uint32_t family_id) const;
};

class Gateway {
 public:
  [[nodiscard]] Result<Bytes> translate(const CapabilitySet& from, const CapabilitySet& to,
                                        std::uint32_t family_id,
                                        std::span<const std::uint8_t> payload) const;
};

}  // namespace lattice
