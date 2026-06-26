#pragma once

#include "lattice/connection.hpp"

#include <functional>
#include <map>

namespace lattice {

struct GatewayRoute {
  std::uint64_t id{0};
  ChannelId source;
  ChannelId destination;
  std::uint32_t family_id{0};
};

class GatewayPolicy {
 public:
  using Translator = std::function<Result<Bytes>(std::span<const std::uint8_t>)>;

  [[nodiscard]] bool may_forward(const CapabilitySet& from, const CapabilitySet& to,
                                 std::uint32_t family_id) const;
  [[nodiscard]] Result<void> add_translator(std::uint32_t family_id, Translator translator);
  [[nodiscard]] Result<Bytes> translate_payload(std::uint32_t family_id,
                                                std::span<const std::uint8_t> payload) const;

 private:
  std::map<std::uint32_t, Translator> translators_;
};

class Gateway {
 public:
  [[nodiscard]] Result<void> add_translator(std::uint32_t family_id,
                                            GatewayPolicy::Translator translator);
  [[nodiscard]] Result<GatewayRoute> create_route(ChannelId source, ChannelId destination,
                                                  std::uint32_t family_id);
  [[nodiscard]] Result<Bytes> forward(const CapabilitySet& from, const CapabilitySet& to,
                                      GatewayRoute route,
                                      std::span<const std::uint8_t> payload) const;
  [[nodiscard]] Result<Bytes> translate(const CapabilitySet& from, const CapabilitySet& to,
                                        std::uint32_t family_id,
                                        std::span<const std::uint8_t> payload) const;

 private:
  GatewayPolicy policy_;
  std::uint64_t next_route_id_{1};
};

}  // namespace lattice
