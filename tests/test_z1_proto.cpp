// Native tests for z1_proto.h. Run: tests/run.sh
#include <string>
#include <vector>

#include "../components/z1/z1_proto.h"
#include "check.h"

using namespace z1lib;

// Captured from a Z1 on firmware 1.1.3.0.1.13 (name and address anonymised).
static const char *IDLE =
    "<Idle|MPos:-1.0000,-1.0000,-1.0000,0.0000,0.0000|WPos:178.1910,37.8360,43.4366,-90.0000,0.0000"
    "|F:0.0,2000.0,100.0|S:0.0,10000.0,100.0,0,22.1,22.9,0,0,0,0|T:4,1.423,-1|L:0, 0, 0, 0.0,100.0"
    "|C:3,1,0,1|E:0,0,0,40,7610|OTA:0,0>\n";

static void test_crc_and_query() {
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK(crc16_xmodem(check, sizeof(check)) == 0x31C3);  // CRC-16/XMODEM check value
  const auto &q = status_query();
  // Same bytes z1probe sent and the Z1 answered.
  const std::vector<uint8_t> want = {0x86, 0x68, 0x00, 0x04, 0xA1, '?', 0, 0, 0x55, 0xAA};
  CHECK(q.size() == want.size());
  for (size_t i = 0; i < 6; i++) CHECK(q[i] == want[i]);
  CHECK(q[8] == 0x55 && q[9] == 0xAA);
  const uint16_t crc = crc16_xmodem(q.data() + 2, 4);
  CHECK(q[6] == (crc >> 8) && q[7] == (crc & 0xFF));
}

static void test_parser_byte_by_byte_with_junk() {
  auto a = build_frame(TYPE_STATUS, "<Idle|T:1,0,-1>");
  auto b = build_frame(0x90, "hello\n");
  std::vector<uint8_t> stream = {'j', 'u', 'n', 'k', 0x86};
  stream.insert(stream.end(), a.begin(), a.end());
  stream.insert(stream.end(), b.begin(), b.end());
  FrameParser p;
  std::vector<Frame> got;
  for (uint8_t byte : stream) p.feed(&byte, 1, [&](const Frame &f) { got.push_back(f); });
  CHECK(got.size() == 2);
  CHECK(got.size() == 2 && got[0].type == TYPE_STATUS && got[0].payload == "<Idle|T:1,0,-1>");
  CHECK(got.size() == 2 && got[1].type == 0x90 && got[1].payload == "hello\n");
  CHECK(p.dropped == 5);
}

static void test_parser_rejects_bad_crc_and_footer() {
  auto bad = build_frame(TYPE_STATUS, "<Run>");
  bad[bad.size() - 3] ^= 0xFF;  // CRC
  auto badfoot = build_frame(TYPE_STATUS, "<Hold>");
  badfoot.back() = 0;
  auto good = build_frame(TYPE_STATUS, "<Idle>");
  std::vector<uint8_t> s = bad;
  s.insert(s.end(), badfoot.begin(), badfoot.end());
  s.insert(s.end(), good.begin(), good.end());
  FrameParser p;
  std::vector<std::string> got;
  p.feed(s.data(), s.size(), [&](const Frame &f) { got.push_back(f.payload); });
  CHECK(got.size() == 1 && got[0] == "<Idle>");
}

static void test_parser_bounded_on_garbage() {
  FrameParser p;
  std::vector<uint8_t> junk(100000, 0x86);  // header-looking byte, never a frame
  int n = 0;
  p.feed(junk.data(), junk.size(), [&](const Frame &) { n++; });
  CHECK(n == 0);
}

static void test_status_idle() {
  Status st;
  CHECK(parse_status(IDLE, st));
  CHECK(st.state == State::IDLE);
  CHECK(st.has_tool && st.tool == 4 && st.tool_wanted == -1);
  NEAR(st.tool_length, 1.423f, 1e-4);
  CHECK(!st.has_job);
  CHECK(st.halt == 0);
  CHECK(st.model == 3);
  NEAR(st.spindle_rpm, 0.0f, 1e-6);
}

static void test_status_tool_change() {
  Status st;
  CHECK(parse_status("<Tool|WPos:1,2,3|T:4,1.423,2|P:7,79,16>", st));
  CHECK(st.state == State::TOOL);
  CHECK(st.tool == 4 && st.tool_wanted == 2);
  CHECK(st.has_job && st.job_percent == 79 && st.job_seconds == 16);
  CHECK(parse_status("<Run|T:-1,1.423,2|P:7,79,20>", st));
  CHECK(st.tool == -1 && st.tool_wanted == 2);
}

static void test_status_alarm_and_garbage() {
  Status st;
  CHECK(parse_status("<Alarm|T:4,1.4,-1|H:11>", st));
  CHECK(st.state == State::ALARM && st.halt == 11);
  CHECK(std::string(halt_reason(11)) == "Cover open");
  CHECK(std::string(halt_reason(0)).empty());
  CHECK(!parse_status("no status here", st));
  CHECK(parse_status("<Weird|T:x>", st) && st.state == State::UNKNOWN && !st.has_tool);
}

static void test_announcement() {
  Announcement a;
  CHECK(parse_announcement("Makera_Z1_000000,192.168.1.50,2222,0,Tool", a));
  CHECK(a.name == "Makera_Z1_000000" && a.ip == "192.168.1.50" && a.port == 2222);
  CHECK(a.has_state && a.state == State::TOOL);
  CHECK(parse_announcement("Carvera,10.0.0.2,2222,0\n", a) && !a.has_state);
  CHECK(!parse_announcement("garbage", a));
  CHECK(!parse_announcement(",,", a));
}

int main() {
  test_crc_and_query();
  test_parser_byte_by_byte_with_junk();
  test_parser_rejects_bad_crc_and_footer();
  test_parser_bounded_on_garbage();
  test_status_idle();
  test_status_tool_change();
  test_status_alarm_and_garbage();
  test_announcement();
  DONE("z1_proto");
}
