// POSIX socket-backed WiFiClient / WiFiServer / WiFi shims for the HOST
// simulator build only. API mirrors the subset of the Arduino WiFi classes
// used by the ESPHomeAPI library.
#pragma once
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <string>

inline void host_close_fd(int *p) {
  if (p) {
    if (*p >= 0) ::close(*p);
    delete p;
  }
}

class WiFiClient {
 public:
  WiFiClient() : fd_(new int(-1), host_close_fd) {}
  explicit WiFiClient(int fd) : fd_(new int(fd), host_close_fd) {}

  explicit operator bool() const { return *fd_ >= 0; }

  void setNoDelay(bool enable) {
    if (*fd_ < 0) return;
    int flag = enable ? 1 : 0;
    ::setsockopt(*fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
  }

  bool connected() {
    if (*fd_ < 0) return false;
    char b;
    ssize_t n = ::recv(*fd_, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0) return false;  // peer closed
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
    if (n < 0) return false;
    return true;
  }

  int available() {
    if (*fd_ < 0) return 0;
    int n = 0;
    if (::ioctl(*fd_, FIONREAD, &n) < 0) return 0;
    return n;
  }

  int read(uint8_t *buf, int len) {
    if (*fd_ < 0) return 0;
    ssize_t n = ::recv(*fd_, buf, len, MSG_DONTWAIT);
    if (n < 0) return 0;
    return (int)n;
  }

  size_t write(const uint8_t *buf, size_t len) {
    if (*fd_ < 0) return 0;
    size_t total = 0;
    while (total < len) {
      ssize_t n = ::send(*fd_, buf + total, len - total, 0);
      if (n > 0) {
        total += (size_t)n;
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        usleep(500);
        continue;
      }
      break;  // error / closed
    }
    return total;
  }

  void stop() {
    if (*fd_ >= 0) {
      ::close(*fd_);
      *fd_ = -1;
    }
  }

 private:
  std::shared_ptr<int> fd_;
};

class WiFiServer {
 public:
  explicit WiFiServer(uint16_t port) : port_(port) {}

  void begin() {
    signal(SIGPIPE, SIG_IGN);
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    if (::bind(listen_fd_, (sockaddr *)&addr, sizeof(addr)) < 0) {
      std::perror("[sim] bind");
      return;
    }
    ::listen(listen_fd_, 4);
    int flags = ::fcntl(listen_fd_, F_GETFL, 0);
    ::fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);
  }

  void setNoDelay(bool) {}

  WiFiClient accept() {
    if (listen_fd_ < 0) return WiFiClient();
    int c = ::accept(listen_fd_, nullptr, nullptr);
    if (c < 0) return WiFiClient();
    int flags = ::fcntl(c, F_GETFL, 0);
    ::fcntl(c, F_SETFL, flags | O_NONBLOCK);
    int flag = 1;
    ::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    return WiFiClient(c);
  }

 private:
  uint16_t port_;
  int listen_fd_ = -1;
};

// Minimal WiFi global (only macAddress() is used by the library).
class WiFiHostClass {
 public:
  std::string macAddress() { return std::string("AA:BB:CC:DD:EE:F0"); }
};
static WiFiHostClass WiFi;
