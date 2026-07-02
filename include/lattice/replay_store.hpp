#pragma once

#include "lattice/error.hpp"

#include <filesystem>
#include <string>

namespace lattice {

class ReplaySnapshotStore {
 public:
  [[nodiscard]] static Result<std::string> load_text(const std::filesystem::path& path);
  [[nodiscard]] static Result<void> save_text(const std::filesystem::path& path,
                                              const std::string& text);
};

}  // namespace lattice
