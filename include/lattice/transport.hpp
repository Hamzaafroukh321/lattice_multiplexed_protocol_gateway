#pragma once

#include "lattice/types.hpp"
#include "lattice/error.hpp"

#include <deque>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace lattice {

class MemoryTransport {
 public:
  void write(Bytes bytes);
  [[nodiscard]] std::optional<Bytes> read();
  [[nodiscard]] std::size_t queued_bytes() const { return queued_bytes_; }
  [[nodiscard]] bool empty() const { return queue_.empty(); }
  void close() { closed_ = true; }
  [[nodiscard]] bool closed() const { return closed_; }

 private:
  std::deque<Bytes> queue_;
  std::size_t queued_bytes_{0};
  bool closed_{false};
};

struct MemoryPipe {
  MemoryTransport left_to_right;
  MemoryTransport right_to_left;
};

class UnixTransport {
 public:
  UnixTransport() = default;
  explicit UnixTransport(int fd);
  UnixTransport(const UnixTransport&) = delete;
  UnixTransport& operator=(const UnixTransport&) = delete;
  UnixTransport(UnixTransport&& other) noexcept;
  UnixTransport& operator=(UnixTransport&& other) noexcept;
  ~UnixTransport();

  [[nodiscard]] static Result<UnixTransport> connect_path(const std::string& path);
  [[nodiscard]] static Result<std::pair<UnixTransport, UnixTransport>> pair_for_test();

  [[nodiscard]] Result<void> write(std::span<const std::uint8_t> bytes);
  [[nodiscard]] Result<Bytes> read_some(std::size_t max_bytes);
  void close();
  [[nodiscard]] bool valid() const { return fd_ >= 0; }
  [[nodiscard]] int native_handle() const { return fd_; }

 private:
  int fd_{-1};
};

class UnixListener {
 public:
  UnixListener() = default;
  explicit UnixListener(int fd, std::string path);
  UnixListener(const UnixListener&) = delete;
  UnixListener& operator=(const UnixListener&) = delete;
  UnixListener(UnixListener&& other) noexcept;
  UnixListener& operator=(UnixListener&& other) noexcept;
  ~UnixListener();

  [[nodiscard]] static Result<UnixListener> bind_path(const std::string& path);
  [[nodiscard]] Result<UnixTransport> accept_one();
  void close();
  [[nodiscard]] bool valid() const { return fd_ >= 0; }

 private:
  int fd_{-1};
  std::string path_;
};

}  // namespace lattice
