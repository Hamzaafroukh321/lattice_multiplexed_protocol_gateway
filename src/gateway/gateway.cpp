#include "lattice/gateway.hpp"

#include <algorithm>

namespace lattice {

bool GatewayPolicy::may_forward(const CapabilitySet& from, const CapabilitySet& to,
                                std::uint32_t family_id) const {
  const auto has_family = [family_id](const CapabilitySet& caps) {
    return std::any_of(caps.plugins.begin(), caps.plugins.end(),
                       [family_id](const PluginDescriptor& descriptor) {
                         return descriptor.family_id == family_id;
                       });
  };
  if (!has_family(from) || !has_family(to)) {
    return false;
  }
  const auto from_it = std::find_if(from.plugins.begin(), from.plugins.end(),
                                    [family_id](const PluginDescriptor& descriptor) {
                                      return descriptor.family_id == family_id;
                                    });
  const auto to_it = std::find_if(to.plugins.begin(), to.plugins.end(),
                                  [family_id](const PluginDescriptor& descriptor) {
                                    return descriptor.family_id == family_id;
                                  });
  return from_it != from.plugins.end() && to_it != to.plugins.end() &&
         from_it->schema_hash == to_it->schema_hash;
}

Result<void> GatewayPolicy::add_translator(std::uint32_t family_id, Translator translator) {
  if (family_id == 0U || !translator) {
    return make_error(ErrorScope::plugin, ErrorCode::plugin_decode, CloseAction::reject_message,
                      "gateway translator descriptor is invalid");
  }
  translators_[family_id] = std::move(translator);
  return {};
}

Result<Bytes> GatewayPolicy::translate_payload(std::uint32_t family_id,
                                               std::span<const std::uint8_t> payload) const {
  const auto it = translators_.find(family_id);
  if (it == translators_.end()) {
    return Bytes(payload.begin(), payload.end());
  }
  return it->second(payload);
}

Result<void> Gateway::add_translator(std::uint32_t family_id,
                                     GatewayPolicy::Translator translator) {
  return policy_.add_translator(family_id, std::move(translator));
}

Result<GatewayRoute> Gateway::create_route(ChannelId source, ChannelId destination,
                                           std::uint32_t family_id) {
  if (source.is_control() || destination.is_control() || family_id == 0U) {
    return make_error(ErrorScope::channel, ErrorCode::illegal_state, CloseAction::reject_message,
                      "gateway route requires two data channels and a family");
  }
  GatewayRoute route;
  route.id = next_route_id_++;
  route.source = source;
  route.destination = destination;
  route.family_id = family_id;
  return route;
}

Result<Bytes> Gateway::forward(const CapabilitySet& from, const CapabilitySet& to,
                               GatewayRoute route,
                               std::span<const std::uint8_t> payload) const {
  if (route.id == 0U || route.family_id == 0U) {
    return make_error(ErrorScope::channel, ErrorCode::illegal_state, CloseAction::reject_message,
                      "gateway route is not initialized");
  }
  return translate(from, to, route.family_id, payload);
}

Result<Bytes> Gateway::translate(const CapabilitySet& from, const CapabilitySet& to,
                                 std::uint32_t family_id,
                                 std::span<const std::uint8_t> payload) const {
  if (!policy_.may_forward(from, to, family_id)) {
    return make_error(ErrorScope::plugin, ErrorCode::plugin_decode, CloseAction::reject_message,
                      "schema-mismatched opaque forwarding rejected");
  }
  auto translated = policy_.translate_payload(family_id, payload);
  if (!translated) {
    return translated.error();
  }
  if (translated.value().size() > to.max_message) {
    return make_error(ErrorScope::message, ErrorCode::resource_limit, CloseAction::reject_message,
                      "translated payload exceeds destination limit");
  }
  return translated.value();
}

}  // namespace lattice
