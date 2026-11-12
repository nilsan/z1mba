#pragma once
// Non-blocking network side: the UDP announcement listener and one TCP status
// query at a time (connect, send '?', read the reply, close). Both are driven
// from loop() and never block it.
//
// Plain POSIX sockets. ESP-IDF maps these onto lwIP, so the same code builds
// natively and tests/test_z1_client.cpp runs it against a fake Z1 on localhost.
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "z1_proto.h"

namespace z1lib {

namespace detail {
inline bool set_nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
}  // namespace detail

class UdpListener {
 public:
  ~UdpListener() { close(); }

  // port 0 picks a free port (tests); local_port() tells which.
  bool open(uint16_t port) {
    close();
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;
    int one = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd_, (sockaddr *) &addr, sizeof(addr)) != 0 || !detail::set_nonblocking(fd_)) {
      close();
      return false;
    }
    return true;
  }

  bool is_open() const { return fd_ >= 0; }

  uint16_t local_port() const {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (fd_ < 0 || ::getsockname(fd_, (sockaddr *) &addr, &len) != 0) return 0;
    return ntohs(addr.sin_port);
  }

  // Calls on_packet(text) for each datagram waiting, at most `max` of them.
  template<typename F> void poll(F &&on_packet, int max = 8) {
    char buf[256];
    for (int i = 0; i < max && fd_ >= 0; i++) {
      const ssize_t n = ::recvfrom(fd_, buf, sizeof(buf) - 1, 0, nullptr, nullptr);
      if (n <= 0) return;
      on_packet(std::string(buf, (size_t) n));
    }
  }

  void close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

 private:
  int fd_ = -1;
};

class StatusClient {
 public:
  enum class Result : uint8_t { PENDING, OK, FAILED };

  ~StatusClient() { close(); }

  bool busy() const { return phase_ != Phase::IDLE; }

  // Starts one query. Returns false (and sets error()) if it can't even start.
  bool start(const std::string &ip, uint16_t port, uint32_t now, uint32_t timeout_ms = 3000) {
    close();
    parser_.reset();
    error_.clear();
    started_ = now;
    timeout_ = timeout_ms;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) return fail("bad address " + ip);
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0 || !detail::set_nonblocking(fd_)) return fail("socket");
    if (::connect(fd_, (sockaddr *) &addr, sizeof(addr)) != 0 && errno != EINPROGRESS) return fail("connect");
    phase_ = Phase::CONNECTING;
    return true;
  }

  // Call from loop(). OK: status() holds the reply. FAILED: error() says why.
  Result poll(uint32_t now) {
    if (phase_ == Phase::IDLE) return Result::FAILED;
    if (now - started_ >= timeout_) {
      fail(phase_ == Phase::CONNECTING ? "connect timeout" : "no reply");
      return Result::FAILED;
    }
    if (phase_ == Phase::CONNECTING) {
      if (!writable()) return Result::PENDING;
      int err = 0;
      socklen_t len = sizeof(err);
      if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
        fail(std::string("connect: ") + std::strerror(err ? err : errno));
        return Result::FAILED;
      }
      const auto &q = status_query();
      if (::send(fd_, q.data(), q.size(), 0) != (ssize_t) q.size()) {
        fail("send");
        return Result::FAILED;
      }
      phase_ = Phase::READING;
    }
    uint8_t buf[512];
    while (true) {
      const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
      if (n == 0) {
        fail("closed by the machine");
        return Result::FAILED;
      }
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return Result::PENDING;
        fail("recv");
        return Result::FAILED;
      }
      bool got = false;
      parser_.feed(buf, (size_t) n, [&](const Frame &f) {
        if (!got && f.type == TYPE_STATUS && parse_status(f.payload, status_)) got = true;
      });
      if (got) {
        close();
        return Result::OK;
      }
    }
  }

  const Status &status() const { return status_; }
  const std::string &error() const { return error_; }

  void close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    phase_ = Phase::IDLE;
  }

 private:
  enum class Phase : uint8_t { IDLE, CONNECTING, READING };

  bool writable() const {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(fd_, &w);
    timeval tv{0, 0};
    return ::select(fd_ + 1, nullptr, &w, nullptr, &tv) > 0;
  }

  bool fail(const std::string &why) {
    error_ = why;
    close();
    return false;
  }

  int fd_ = -1;
  Phase phase_ = Phase::IDLE;
  uint32_t started_ = 0, timeout_ = 3000;
  FrameParser parser_;
  Status status_;
  std::string error_;
};

}  // namespace z1lib
