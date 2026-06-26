#pragma once

#include "lattice/channel.hpp"
#include "lattice/error.hpp"
#include "lattice/types.hpp"

#include <functional>
#include <map>
#include <memory>
#include <utility>

namespace lattice {

class Plugin {
 public:
  virtual ~Plugin() = default;
  [[nodiscard]] virtual PluginDescriptor descriptor() const = 0;
  [[nodiscard]] virtual Result<Bytes> handle(const LogicalMessage& message) = 0;
};

class EchoPlugin final : public Plugin {
 public:
  [[nodiscard]] PluginDescriptor descriptor() const override;
  [[nodiscard]] Result<Bytes> handle(const LogicalMessage& message) override;
};

struct PluginCompletion {
  std::uint64_t token{0};
  ChannelId channel;
  std::uint32_t sequence{0};
  std::uint32_t family_id{0};
  Bytes response;
};

struct PluginLeaseState {
  std::size_t active{0};
  bool draining{false};
};

class PluginLease {
 public:
  PluginLease() = default;
  PluginLease(std::unique_ptr<Plugin> plugin, std::shared_ptr<PluginLeaseState> state);
  PluginLease(const PluginLease&) = delete;
  PluginLease& operator=(const PluginLease&) = delete;
  PluginLease(PluginLease&& other) noexcept;
  PluginLease& operator=(PluginLease&& other) noexcept;
  ~PluginLease();

  [[nodiscard]] explicit operator bool() const { return plugin_ != nullptr; }
  [[nodiscard]] Plugin* operator->() const { return plugin_.get(); }
  [[nodiscard]] Plugin& operator*() const { return *plugin_; }

 private:
  void release();

  std::unique_ptr<Plugin> plugin_;
  std::shared_ptr<PluginLeaseState> state_;
};

class PluginRegistry {
 public:
  using Factory = std::function<std::unique_ptr<Plugin>()>;

  [[nodiscard]] Result<void> register_factory(PluginDescriptor descriptor, Factory factory);
  [[nodiscard]] Result<void> unregister_family(std::uint32_t family_id);
  [[nodiscard]] Result<PluginLease> create_lease(std::uint32_t family_id) const;
  [[nodiscard]] Result<std::unique_ptr<Plugin>> create(std::uint32_t family_id) const;
  [[nodiscard]] std::vector<PluginDescriptor> descriptors() const;
  [[nodiscard]] bool contains_exact(const PluginDescriptor& descriptor) const;

 private:
  struct Entry {
    PluginDescriptor descriptor;
    Factory factory;
    std::shared_ptr<PluginLeaseState> state;
  };
  std::map<std::uint32_t, Entry> entries_;
};

}  // namespace lattice
