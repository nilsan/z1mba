// Native tests for z1_client.h against a fake Z1 on localhost: one thread
// plays the machine (TCP status replies, UDP announcements).
#include <atomic>
#include <chrono>
#include <thread>

#include "../components/z1/z1_client.h"
#include "check.h"

using namespace z1lib;

static uint32_t now_ms() {
  using namespace std::chrono;
  return (uint32_t) duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// mode: 'a' answer with a status frame, 'c' close at once, 's' stay silent,
// 'j' answer with junk first and then a split status frame.
struct FakeZ1 {
  int srv = -1;
  uint16_t port = 0;
  std::thread th;
  explicit FakeZ1(char mode) {
    srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(srv, (sockaddr *) &a, sizeof(a));
    ::listen(srv, 4);
    socklen_t len = sizeof(a);
    ::getsockname(srv, (sockaddr *) &a, &len);
    port = ntohs(a.sin_port);
    th = std::thread([this, mode] {
      int c = ::accept(srv, nullptr, nullptr);
      if (c < 0) return;
      if (mode == 'c') {
        ::close(c);
        return;
      }
      uint8_t buf[64];
      if (::recv(c, buf, sizeof(buf), 0) == 10 && buf[4] == 0xA1 && buf[5] == '?' && mode != 's') {
        auto f = build_frame(TYPE_STATUS, "<Tool|T:4,1.423,2|P:7,79,16>\n");
        if (mode == 'j') {
          ::send(c, "\x01\x02junk", 6, 0);
          ::send(c, f.data(), 7, 0);
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          ::send(c, f.data() + 7, f.size() - 7, 0);
        } else {
          ::send(c, f.data(), f.size(), 0);
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(mode == 's' ? 600 : 100));
      ::close(c);
    });
  }
  ~FakeZ1() {
    th.join();
    ::close(srv);
  }
};

static StatusClient::Result query(uint16_t port, StatusClient &c, uint32_t timeout_ms = 2000) {
  const uint32_t t0 = now_ms();
  if (!c.start("127.0.0.1", port, t0, timeout_ms)) return StatusClient::Result::FAILED;
  while (true) {
    auto r = c.poll(now_ms());
    if (r != StatusClient::Result::PENDING) return r;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

static void test_answer() {
  for (char mode : {'a', 'j'}) {
    FakeZ1 z(mode);
    StatusClient c;
    CHECK(query(z.port, c) == StatusClient::Result::OK);
    CHECK(c.status().state == State::TOOL && c.status().tool_wanted == 2);
    CHECK(!c.busy());
  }
}

static void test_closed_and_silent() {
  {
    FakeZ1 z('c');
    StatusClient c;
    CHECK(query(z.port, c) == StatusClient::Result::FAILED);
    CHECK(!c.error().empty());
  }
  {
    FakeZ1 z('s');
    StatusClient c;
    CHECK(query(z.port, c, 300) == StatusClient::Result::FAILED);
    CHECK(c.error() == "no reply");
  }
}

static void test_refused_and_bad_address() {
  StatusClient c;
  // Port 1 on loopback: nothing listens there.
  CHECK(query(1, c) == StatusClient::Result::FAILED);
  CHECK(c.error().rfind("connect", 0) == 0);
  CHECK(!c.start("not-an-ip", 2222, now_ms()));
  CHECK(c.error() == "bad address not-an-ip");
}

static void test_udp_listener() {
  UdpListener l;
  CHECK(l.open(0));
  const uint16_t port = l.local_port();
  CHECK(port != 0);
  int s = ::socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const char *msg = "Makera_Z1_000000,192.168.1.50,2222,0,Tool";
  ::sendto(s, msg, std::strlen(msg), 0, (sockaddr *) &a, sizeof(a));
  ::close(s);
  std::string got;
  for (int i = 0; i < 100 && got.empty(); i++) {
    l.poll([&](const std::string &p) { got = p; });
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  Announcement an;
  CHECK(parse_announcement(got, an) && an.state == State::TOOL);
  int calls = 0;
  l.poll([&](const std::string &) { calls++; });  // nothing waiting: returns at once
  CHECK(calls == 0);
}

int main() {
  test_answer();
  test_closed_and_silent();
  test_refused_and_bad_address();
  test_udp_listener();
  DONE("z1_client");
}
