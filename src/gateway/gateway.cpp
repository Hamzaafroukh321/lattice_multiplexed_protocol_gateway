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
  if (from_it == from.plugins.end() || to_it == to.plugins.end()) {
    return false;
  }
  return from_it->schema_hash == to_it->schema_hash || has_translator(family_id);
}

bool GatewayPolicy::has_translator(std::uint32_t family_id) const {
  return translators_.find(family_id) != translators_.end();
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
  routes_[route.id] = route;
  return route;
}

Result<GatewayRoute> Gateway::find_route(ChannelId source) const {
  const auto it = std::find_if(routes_.begin(), routes_.end(),
                               [source](const auto& entry) {
                                 return entry.second.source == source;
                               });
  if (it == routes_.end()) {
    return make_error(ErrorScope::channel, ErrorCode::illegal_state, CloseAction::reject_message,
                      "gateway route source is not registered");
  }
  return it->second;
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

Result<GatewayForwardedMessage> Gateway::bridge_message(
    const CapabilitySet& from, const CapabilitySet& to, ChannelId source,
    std::span<const std::uint8_t> payload) const {
  auto route = find_route(source);
  if (!route) {
    return route.error();
  }
  auto translated = forward(from, to, route.value(), payload);
  if (!translated) {
    return translated.error();
  }
  GatewayForwardedMessage message;
  message.route_id = route.value().id;
  message.source = route.value().source;
  message.destination = route.value().destination;
  message.family_id = route.value().family_id;
  message.payload = translated.take_value();
  return message;
}

Result<std::vector<Bytes>> Gateway::bridge_to_connection(
    const CapabilitySet& from, const CapabilitySet& to, ConnectionEngine& destination,
    ChannelId source, std::span<const std::uint8_t> payload) const {
  auto message = bridge_message(from, to, source, payload);
  if (!message) {
    return message.error();
  }
  return destination.send(message.value().destination, message.value().payload);
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
