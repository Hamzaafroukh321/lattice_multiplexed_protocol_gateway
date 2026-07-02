#pragma once

#include "lattice/connection.hpp"

#include <functional>
#include <map>
#include <vector>

namespace lattice {

struct GatewayRoute {
  std::uint64_t id{0};
  ChannelId source;
  ChannelId destination;
  std::uint32_t family_id{0};
};

struct GatewayForwardedMessage {
  std::uint64_t route_id{0};
  ChannelId source;
  ChannelId destination;
  std::uint32_t family_id{0};
  Bytes payload;
};

class GatewayPolicy {
 public:
  using Translator = std::function<Result<Bytes>(std::span<const std::uint8_t>)>;

  [[nodiscard]] bool may_forward(const CapabilitySet& from, const CapabilitySet& to,
                                 std::uint32_t family_id) const;
  [[nodiscard]] bool has_translator(std::uint32_t family_id) const;
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
  [[nodiscard]] Result<GatewayRoute> find_route(ChannelId source) const;
  [[nodiscard]] Result<Bytes> forward(const CapabilitySet& from, const CapabilitySet& to,
                                      GatewayRoute route,
                                      std::span<const std::uint8_t> payload) const;
  [[nodiscard]] Result<GatewayForwardedMessage> bridge_message(
      const CapabilitySet& from, const CapabilitySet& to, ChannelId source,
      std::span<const std::uint8_t> payload) const;
  [[nodiscard]] Result<std::vector<Bytes>> bridge_to_connection(
      const CapabilitySet& from, const CapabilitySet& to, ConnectionEngine& destination,
      ChannelId source, std::span<const std::uint8_t> payload) const;
  [[nodiscard]] Result<Bytes> translate(const CapabilitySet& from, const CapabilitySet& to,
                                        std::uint32_t family_id,
                                        std::span<const std::uint8_t> payload) const;

 private:
  GatewayPolicy policy_;
  std::map<std::uint64_t, GatewayRoute> routes_;
  std::uint64_t next_route_id_{1};
};

}  // namespace lattice
