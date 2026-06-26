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

Result<Bytes> Gateway::translate(const CapabilitySet& from, const CapabilitySet& to,
                                 std::uint32_t family_id,
                                 std::span<const std::uint8_t> payload) const {
  GatewayPolicy policy;
  if (!policy.may_forward(from, to, family_id)) {
    return make_error(ErrorScope::plugin, ErrorCode::plugin_decode, CloseAction::reject_message,
                      "schema-mismatched opaque forwarding rejected");
  }
  if (payload.size() > to.max_message) {
    return make_error(ErrorScope::message, ErrorCode::resource_limit, CloseAction::reject_message,
                      "translated payload exceeds destination limit");
  }
  return Bytes(payload.begin(), payload.end());
}

}  // namespace lattice
