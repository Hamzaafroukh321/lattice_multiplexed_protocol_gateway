#include "lattice/plugin.hpp"

#include <algorithm>

namespace lattice {

PluginDescriptor EchoPlugin::descriptor() const {
  return PluginDescriptor{7U, 0x4543484F5F763031ULL, 4U};
}

Result<Bytes> EchoPlugin::handle(const LogicalMessage& message) {
  Bytes out;
  out.reserve(5U + message.payload.size());
  out.push_back('e');
  out.push_back('c');
  out.push_back('h');
  out.push_back('o');
  out.push_back(':');
  out.insert(out.end(), message.payload.begin(), message.payload.end());
  return out;
}

PluginLease::PluginLease(std::unique_ptr<Plugin> plugin, std::shared_ptr<PluginLeaseState> state)
    : plugin_(std::move(plugin)), state_(std::move(state)) {
  if (state_) {
    ++state_->active;
  }
}

PluginLease::PluginLease(PluginLease&& other) noexcept
    : plugin_(std::move(other.plugin_)), state_(std::move(other.state_)) {}

PluginLease& PluginLease::operator=(PluginLease&& other) noexcept {
  if (this != &other) {
    release();
    plugin_ = std::move(other.plugin_);
    state_ = std::move(other.state_);
  }
  return *this;
}

PluginLease::~PluginLease() {
  release();
}

void PluginLease::release() {
  plugin_.reset();
  if (state_) {
    if (state_->active > 0U) {
      --state_->active;
    }
    state_.reset();
  }
}

Result<void> PluginRegistry::register_factory(PluginDescriptor descriptor, Factory factory) {
  if (descriptor.family_id == 0U || descriptor.max_depth > 32U || !factory) {
    return make_error(ErrorScope::plugin, ErrorCode::resource_limit,
                      CloseAction::reject_message, "invalid plugin descriptor");
  }
  const auto state = std::make_shared<PluginLeaseState>();
  const auto [it, inserted] = entries_.emplace(descriptor.family_id, Entry{descriptor, std::move(factory), state});
  if (!inserted) {
    if (!(it->second.descriptor == descriptor)) {
      return make_error(ErrorScope::plugin, ErrorCode::duplicate_required_tlv,
                        CloseAction::close_connection, "ambiguous plugin schema");
    }
    if (it->second.state && it->second.state->active > 0U) {
      return make_error(ErrorScope::plugin, ErrorCode::would_block, CloseAction::none,
                        "cannot replace plugin family while leases are active");
    }
    it->second.factory = std::move(factory);
    it->second.state = state;
  }
  return {};
}

Result<void> PluginRegistry::unregister_family(std::uint32_t family_id) {
  const auto it = entries_.find(family_id);
  if (it == entries_.end()) {
    return make_error(ErrorScope::plugin, ErrorCode::plugin_decode, CloseAction::reject_message,
                      "plugin family not registered");
  }
  if (it->second.state) {
    it->second.state->draining = true;
    if (it->second.state->active > 0U) {
      return make_error(ErrorScope::plugin, ErrorCode::would_block, CloseAction::none,
                        "plugin family is waiting for active leases");
    }
  }
  entries_.erase(it);
  return {};
}

Result<PluginLease> PluginRegistry::create_lease(std::uint32_t family_id) const {
  const auto it = entries_.find(family_id);
  if (it == entries_.end() || !it->second.state || it->second.state->draining) {
    return make_error(ErrorScope::plugin, ErrorCode::plugin_decode, CloseAction::reject_message,
                      "plugin family is unavailable");
  }
  return PluginLease(it->second.factory(), it->second.state);
}

Result<std::unique_ptr<Plugin>> PluginRegistry::create(std::uint32_t family_id) const {
  const auto it = entries_.find(family_id);
  if (it == entries_.end() || !it->second.state || it->second.state->draining) {
    return make_error(ErrorScope::plugin, ErrorCode::plugin_decode, CloseAction::reject_message,
                      "plugin family is unavailable");
  }
  return it->second.factory();
}

std::vector<PluginDescriptor> PluginRegistry::descriptors() const {
  std::vector<PluginDescriptor> out;
  out.reserve(entries_.size());
  for (const auto& [family, entry] : entries_) {
    (void)family;
    out.push_back(entry.descriptor);
  }
  std::sort(out.begin(), out.end(), [](const PluginDescriptor& a, const PluginDescriptor& b) {
    return a.family_id < b.family_id;
  });
  return out;
}

bool PluginRegistry::contains_exact(const PluginDescriptor& descriptor) const {
  const auto it = entries_.find(descriptor.family_id);
  return it != entries_.end() && it->second.descriptor == descriptor;
}

}  // namespace lattice
