#include "lattice/transport.hpp"

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace lattice {
namespace {

[[nodiscard]] Error transport_error(std::string detail) {
  return make_error(ErrorScope::transport, ErrorCode::transport_error,
                    CloseAction::close_connection, std::move(detail));
}

}  // namespace

UnixTransport::UnixTransport(int fd) : fd_(fd) {}

UnixTransport::UnixTransport(UnixTransport&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

UnixTransport& UnixTransport::operator=(UnixTransport&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

UnixTransport::~UnixTransport() {
  close();
}

Result<UnixTransport> UnixTransport::connect_path(const std::string& path) {
#ifdef _WIN32
  (void)path;
  return transport_error("Unix-domain sockets are not available on this platform");
#else
  if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
    return transport_error("Unix socket path is empty or too long");
  }
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return transport_error(std::string("socket failed: ") + std::strerror(errno));
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1U);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    const std::string detail = std::string("connect failed: ") + std::strerror(errno);
    ::close(fd);
    return transport_error(detail);
  }
  return UnixTransport(fd);
#endif
}

Result<std::pair<UnixTransport, UnixTransport>> UnixTransport::pair_for_test() {
#ifdef _WIN32
  return transport_error("Unix-domain socketpair is not available on this platform");
#else
  int fds[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    return transport_error(std::string("socketpair failed: ") + std::strerror(errno));
  }
  return std::make_pair(UnixTransport(fds[0]), UnixTransport(fds[1]));
#endif
}

Result<void> UnixTransport::write(std::span<const std::uint8_t> bytes) {
#ifdef _WIN32
  (void)bytes;
  return transport_error("Unix-domain sockets are not available on this platform");
#else
  if (fd_ < 0) {
    return transport_error("write on closed Unix transport");
  }
  std::size_t sent = 0;
  while (sent < bytes.size()) {
    const ssize_t n = ::send(fd_, bytes.data() + sent, bytes.size() - sent, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return transport_error(std::string("send failed: ") + std::strerror(errno));
    }
    if (n == 0) {
      return transport_error("send returned zero bytes");
    }
    sent += static_cast<std::size_t>(n);
  }
  return {};
#endif
}

Result<Bytes> UnixTransport::read_some(std::size_t max_bytes) {
#ifdef _WIN32
  (void)max_bytes;
  return transport_error("Unix-domain sockets are not available on this platform");
#else
  if (fd_ < 0) {
    return transport_error("read on closed Unix transport");
  }
  Bytes out(max_bytes);
  const ssize_t n = ::recv(fd_, out.data(), out.size(), 0);
  if (n < 0) {
    if (errno == EINTR) {
      return Bytes{};
    }
    return transport_error(std::string("recv failed: ") + std::strerror(errno));
  }
  out.resize(static_cast<std::size_t>(n));
  return out;
#endif
}

void UnixTransport::close() {
#ifndef _WIN32
  if (fd_ >= 0) {
    (void)::close(fd_);
  }
#endif
  fd_ = -1;
}

}  // namespace lattice
