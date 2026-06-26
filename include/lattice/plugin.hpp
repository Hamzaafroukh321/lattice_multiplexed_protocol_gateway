#pragma once

#include "lattice/channel.hpp"
#include "lattice/error.hpp"
#include "lattice/types.hpp"

#include <functional>
#include <map>
#include <memory>

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

class PluginRegistry {
 public:
  using Factory = std::function<std::unique_ptr<Plugin>()>;

  [[nodiscard]] Result<void> register_factory(PluginDescriptor descriptor, Factory factory);
  [[nodiscard]] Result<void> unregister_family(std::uint32_t family_id);
  [[nodiscard]] Result<std::unique_ptr<Plugin>> create(std::uint32_t family_id) const;
  [[nodiscard]] std::vector<PluginDescriptor> descriptors() const;
  [[nodiscard]] bool contains_exact(const PluginDescriptor& descriptor) const;

 private:
  struct Entry {
    PluginDescriptor descriptor;
    Factory factory;
    bool draining{false};
  };
  std::map<std::uint32_t, Entry> entries_;
};

}  // namespace lattice
