// Native tests for z1_tracker.h, including a replay of the real Studio session
// (tests/fixtures/studio-session.txt, made with tools/make_fixture.py).
#include <fstream>
#include <sstream>
#include <string>

#include "../components/z1/z1_tracker.h"
#include "check.h"

using namespace z1lib;

static Announcement ann(const char *state) {
  Announcement a;
  parse_announcement(std::string("Makera_Z1_000000,192.168.1.50,2222,0,") + state, a);
  return a;
}
static Status status(const char *text) {
  Status s;
  parse_status(text, s);
  return s;
}

static void test_no_target_no_poll() {
  Tracker t;
  CHECK(!t.should_poll(0));
  CHECK(!t.online(0));
  CHECK(t.state(0) == State::UNKNOWN);
  CHECK(t.pending_tool(0) == -1);
}

static void test_discovery_and_schedule() {
  Tracker t;
  t.on_announcement(1000, ann("Idle"));
  CHECK(t.ip() == "192.168.1.50" && t.port() == 2222);
  CHECK(t.should_poll(1000));  // first poll right away
  t.poll_started(1000);
  t.on_status(1020, status("<Idle|T:4,1.423,-1>"));
  CHECK(!t.should_poll(1500));
  CHECK(t.interval(1500) == 10000);
  t.on_announcement(2000, ann("Idle"));  // same state: no extra poll
  CHECK(!t.should_poll(2000));
  t.on_announcement(3000, ann("Run"));  // change: poll now
  CHECK(t.state(3000) == State::RUN);
  CHECK(t.should_poll(3000));
  t.poll_started(3000);
  t.on_status(3020, status("<Run|T:4,1.423,-1|P:0,58,2>"));
  CHECK(t.job_active(3020) && t.interval(3020) == 2000);
  CHECK(!t.should_poll(4000) && t.should_poll(5000));
}

static void test_name_filter_and_fixed_ip() {
  Tracker t;
  t.set_name_filter("Makera_Z1_999999");
  CHECK(!t.on_announcement(0, ann("Idle")));
  CHECK(!t.has_target());
  Tracker f;
  f.set_target("192.168.1.99", 2222);
  CHECK(!f.on_announcement(0, ann("Idle")));  // another machine
  CHECK(f.ip() == "192.168.1.99");
}

static void test_offline_backoff() {
  Tracker t;
  t.set_target("192.168.1.50", 2222);
  t.poll_started(0);
  for (int i = 0; i < 3; i++) t.on_poll_failed(0);
  CHECK(!t.online(0));
  CHECK(t.interval(0) == 30000);
  t.on_status(100, status("<Idle|T:4,1,-1>"));
  CHECK(t.online(100));
}

static void test_announcements_alone_keep_online() {
  Tracker t;
  t.on_announcement(0, ann("Idle"));
  t.poll_started(0);
  for (int i = 0; i < 5; i++) t.on_poll_failed(0);
  CHECK(t.online(1000));  // still announcing
  CHECK(t.state(1000) == State::IDLE);
  CHECK(!t.online(6000));  // announcements stopped 6 s ago
}

static void test_pending_tool_rules() {
  Tracker t;
  t.on_announcement(0, ann("Run"));
  t.on_status(10, status("<Run|T:4,1.423,-1|P:7,75,5>"));
  CHECK(t.pending_tool(10) == -1);  // nothing asked yet
  t.on_status(20, status("<Run|T:4,1.423,2|P:7,79,9>"));
  CHECK(t.pending_tool(20) == 2);  // M6 on its way, before Tool
  CHECK(t.interval(20) == 1000);
  t.on_status(30, status("<Run|T:-1,1.423,2|P:7,79,20>"));
  CHECK(t.pending_tool(30) == -1);  // measuring
  t.on_status(40, status("<Run|T:2,1.380,2|P:7,79,28>"));
  CHECK(t.pending_tool(40) == -1);  // done; wanted stays 2 but equals active
}

static void test_newer_source_wins() {
  Tracker t;
  t.on_announcement(0, ann("Run"));
  t.on_status(100, status("<Run|T:4,1,2|P:1,1,1>"));
  t.on_announcement(500, ann("Tool"));  // UDP is newer than the last poll
  CHECK(t.state(500) == State::TOOL && t.tool_change(500));
  t.on_status(520, status("<Tool|T:4,1,2|P:1,1,1>"));
  CHECK(t.state(520) == State::TOOL);
  t.on_status(4000, status("<Run|T:-1,1,2|P:1,1,1>"));  // poll newer than UDP
  CHECK(t.state(4000) == State::RUN);
}

static void test_wraparound() {
  Tracker t;
  const uint32_t near_wrap = 0xFFFFFF00u;
  t.on_announcement(near_wrap, ann("Idle"));
  t.poll_started(near_wrap);
  t.on_status(near_wrap + 10, status("<Idle|T:4,1,-1>"));
  CHECK(!t.should_poll(near_wrap + 5000));
  CHECK(t.should_poll(near_wrap + 10000));  // wrapped past zero
}

// Replay the real session: every change in the UDP state must ask for a poll,
// the tracker must report both tool changes, and the wanted tool must show up
// before the machine stops.
static void test_replay_studio_session(const char *path) {
  std::ifstream in(path);
  CHECK(in.good());
  Tracker t;
  std::string line;
  int udp_changes = 0, poll_asked = 0, tool_events = 0, lines = 0;
  bool saw_pending_2 = false, saw_pending_4 = false;
  State last = State::UNKNOWN;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    uint32_t ms;
    char kind;
    ls >> ms >> kind;
    std::string text;
    std::getline(ls, text);
    if (!text.empty() && text[0] == ' ') text.erase(0, 1);
    lines++;
    if (kind == 'U') {
      Announcement a;
      CHECK(parse_announcement(text, a));
      const bool changed = a.state != last;
      t.on_announcement(ms, a);
      if (changed) {
        udp_changes++;
        if (t.should_poll(ms)) poll_asked++;
        t.poll_started(ms);
      }
      if (a.state == State::TOOL) {
        tool_events++;
        CHECK(t.tool_change(ms));
      }
      last = a.state;
    } else if (kind == 'S') {
      Status s;
      CHECK(parse_status(text, s));
      t.on_status(ms, s);
      if (t.pending_tool(ms) == 2) saw_pending_2 = true;
      if (t.pending_tool(ms) == 4) saw_pending_4 = true;
      CHECK(t.pending_tool(ms) == -1 || s.tool >= 0);
    } else if (kind == 'F') {
      t.on_poll_failed(ms);
    }
  }
  CHECK(lines > 100);
  CHECK(udp_changes == poll_asked);
  CHECK(tool_events == 2);
  CHECK(saw_pending_2 && saw_pending_4);
}

int main(int argc, char **argv) {
  test_no_target_no_poll();
  test_discovery_and_schedule();
  test_name_filter_and_fixed_ip();
  test_offline_backoff();
  test_announcements_alone_keep_online();
  test_pending_tool_rules();
  test_newer_source_wins();
  test_wraparound();
  test_replay_studio_session(argc > 1 ? argv[1] : "tests/fixtures/studio-session.txt");
  DONE("z1_tracker");
}
