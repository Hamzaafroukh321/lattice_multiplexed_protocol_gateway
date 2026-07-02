#include "lattice/replay_store.hpp"

#include <fstream>

namespace lattice {
namespace {

[[nodiscard]] Error store_error(std::string detail) {
  return make_error(ErrorScope::internal, ErrorCode::resource_limit, CloseAction::none,
                    std::move(detail));
}

}  // namespace

Result<std::string> ReplaySnapshotStore::load_text(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return store_error("cannot open replay snapshot");
  }
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (!in.good() && !in.eof()) {
    return store_error("cannot read replay snapshot");
  }
  return text;
}

Result<void> ReplaySnapshotStore::save_text(const std::filesystem::path& path,
                                            const std::string& text) {
  const std::filesystem::path temp = path.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      return store_error("cannot create replay snapshot temp file");
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
      return store_error("cannot write replay snapshot temp file");
    }
  }

  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
  if (remove_error) {
    std::filesystem::remove(temp);
    return store_error("cannot replace existing replay snapshot");
  }

  std::error_code rename_error;
  std::filesystem::rename(temp, path, rename_error);
  if (rename_error) {
    std::filesystem::remove(temp);
    return store_error("cannot publish replay snapshot");
  }
  return {};
}

}  // namespace lattice
