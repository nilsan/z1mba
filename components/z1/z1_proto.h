#pragma once
// Makera Z1 LAN protocol: frames, the status reply and the UDP announcement.
// Plain C++17 with no ESPHome or ESP-IDF includes, so tests/test_z1_proto.cpp
// builds it natively. What each field means, as tested, is in docs/Z1-PROTOCOL.md.
//
// Frame: 86 68 | len (2, BE) | type (1) | payload | CRC-16/XMODEM (2, BE) | 55 AA
// len = 1 + payload + 2; the CRC covers len, type and payload.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace z1lib {

inline constexpr uint8_t TYPE_CTRL_SINGLE = 0xA1;  // one real-time byte, '?'
inline constexpr uint8_t TYPE_STATUS = 0x81;       // "<Idle|...>"
inline constexpr uint16_t MAX_LEN = 8200;
inline constexpr uint16_t MACHINE_PORT = 2222;
inline constexpr uint16_t DISCOVERY_PORT = 3333;

inline uint16_t crc16_xmodem(const uint8_t *data, size_t n) {
  uint16_t crc = 0;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t) data[i] << 8;
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t) ((crc << 1) ^ 0x1021) : (uint16_t) (crc << 1);
  }
  return crc;
}

inline std::vector<uint8_t> build_frame(uint8_t type, const std::string &payload) {
  const uint16_t len = (uint16_t) (1 + payload.size() + 2);
  std::vector<uint8_t> f = {0x86, 0x68, (uint8_t) (len >> 8), (uint8_t) len, type};
  f.insert(f.end(), payload.begin(), payload.end());
  const uint16_t crc = crc16_xmodem(f.data() + 2, f.size() - 2);
  f.push_back((uint8_t) (crc >> 8));
  f.push_back((uint8_t) crc);
  f.push_back(0x55);
  f.push_back(0xAA);
  return f;
}

// The status query, the only frame z1mba sends.
inline const std::vector<uint8_t> &status_query() {
  static const std::vector<uint8_t> q = build_frame(TYPE_CTRL_SINGLE, "?");
  return q;
}

struct Frame {
  uint8_t type;
  std::string payload;
};

// Incremental parser: feed bytes as they arrive, collect whole frames. Junk,
// bad footers and bad CRCs are skipped (counted in `dropped`) by resyncing on
// the next header byte.
class FrameParser {
 public:
  template<typename F> void feed(const uint8_t *data, size_t n, F &&on_frame) {
    buf_.insert(buf_.end(), data, data + n);
    size_t pos = 0;
    while (true) {
      while (pos < buf_.size() && buf_[pos] != 0x86) pos++, dropped++;
      if (buf_.size() - pos < 4) break;
      if (buf_[pos + 1] != 0x68) {
        pos++, dropped++;
        continue;
      }
      const uint16_t len = (uint16_t) ((buf_[pos + 2] << 8) | buf_[pos + 3]);
      if (len < 3 || len > MAX_LEN) {
        pos++, dropped++;
        continue;
      }
      const size_t total = 2 + 2 + (size_t) len + 2;
      if (buf_.size() - pos < total) break;
      const uint8_t *f = buf_.data() + pos;
      const uint16_t crc = (uint16_t) ((f[total - 4] << 8) | f[total - 3]);
      if (f[total - 2] != 0x55 || f[total - 1] != 0xAA || crc16_xmodem(f + 2, len) != crc) {
        pos++, dropped++;
        continue;
      }
      on_frame(Frame{f[4], std::string((const char *) f + 5, len - 3)});
      pos += total;
    }
    buf_.erase(buf_.begin(), buf_.begin() + (std::ptrdiff_t) pos);
    if (buf_.size() > 4 * (size_t) MAX_LEN) {  // never more than a few frames' worth
      dropped += buf_.size();
      buf_.clear();
    }
  }
  void reset() { buf_.clear(); }
  size_t dropped = 0;

 private:
  std::vector<uint8_t> buf_;
};

enum class State : uint8_t { UNKNOWN, IDLE, RUN, HOLD, PAUSE, WAIT, TOOL, ALARM, HOME, SLEEP };

inline State parse_state(const std::string &s) {
  static const struct {
    const char *name;
    State state;
  } names[] = {{"Idle", State::IDLE}, {"Run", State::RUN},     {"Hold", State::HOLD},
               {"Pause", State::PAUSE}, {"Wait", State::WAIT}, {"Tool", State::TOOL},
               {"Alarm", State::ALARM}, {"Home", State::HOME}, {"Sleep", State::SLEEP}};
  for (const auto &n : names)
    if (s == n.name) return n.state;
  return State::UNKNOWN;
}

inline const char *state_name(State s) {
  switch (s) {
    case State::IDLE: return "Idle";
    case State::RUN: return "Running";
    case State::HOLD: return "Hold";
    case State::PAUSE: return "Paused";
    case State::WAIT: return "Waiting";
    case State::TOOL: return "Tool change";
    case State::ALARM: return "Alarm";
    case State::HOME: return "Homing";
    case State::SLEEP: return "Sleep";
    default: return "Unknown";
  }
}

// Halt reasons (H:), from the Z1 firmware and ha-makera-z1.
inline const char *halt_reason(int code) {
  switch (code) {
    case 1: return "Stopped by hand";
    case 2: return "Homing failed";
    case 3: return "Probing failed";
    case 9: return "Spindle overheated";
    case 10: return "Soft limit";
    case 11: return "Cover open";
    case 12: return "Probe not detected";
    case 13: return "Emergency stop";
    case 14: return "Power supply overheated";
    case 15: return "Not homed";
    case 21: return "Hard limit";
    case 22: return "Motor error X";
    case 23: return "Motor error Y";
    case 24: return "Motor error Z";
    case 25: return "Spindle stalled";
    case 26: return "SD card error";
    case 41: return "Spindle alarm";
    default: return code > 0 ? "Halted" : "";
  }
}

struct Status {
  State state = State::UNKNOWN;
  // T: active tool, tool length offset, wanted tool (-1 = none asked yet).
  bool has_tool = false;
  int tool = -1;
  float tool_length = NAN;
  int tool_wanted = -1;
  // P: present only while a file is loaded. The percentage is bytes read into
  // the planner, not work done (docs/Z1-PROTOCOL.md).
  bool has_job = false;
  int job_percent = 0;
  uint32_t job_seconds = 0;
  int halt = 0;   // H:, 0 = none
  int model = 0;  // C: first field, 3 = Z1, 4 = Z1 Pro
  float spindle_rpm = NAN;
};

namespace detail {
// Splits "a,b,c" into up to n numbers; returns how many were read.
inline int numbers(const std::string &s, double *out, int n) {
  int count = 0;
  const char *p = s.c_str();
  while (count < n && *p) {
    char *end;
    double v = std::strtod(p, &end);
    if (end == p) break;
    out[count++] = v;
    p = end;
    while (*p == ' ') p++;
    if (*p != ',') break;
    p++;
  }
  return count;
}
}  // namespace detail

// "<Run|MPos:...|T:4,1.423,2|P:7,79,9>" -> Status. False if it isn't one.
inline bool parse_status(const std::string &text, Status &st) {
  const size_t open = text.find('<');
  const size_t close = text.find('>', open == std::string::npos ? 0 : open);
  if (open == std::string::npos || close == std::string::npos) return false;
  const std::string body = text.substr(open + 1, close - open - 1);
  st = Status{};
  size_t start = 0;
  bool first = true;
  while (start <= body.size()) {
    size_t bar = body.find('|', start);
    if (bar == std::string::npos) bar = body.size();
    const std::string part = body.substr(start, bar - start);
    if (first) {
      st.state = parse_state(part);
      first = false;
    } else {
      const size_t colon = part.find(':');
      if (colon != std::string::npos) {
        const std::string key = part.substr(0, colon), val = part.substr(colon + 1);
        double v[3];
        const int n = detail::numbers(val, v, 3);
        if (key == "T" && n >= 1) {
          st.has_tool = true;
          st.tool = (int) v[0];
          if (n >= 2) st.tool_length = (float) v[1];
          if (n >= 3) st.tool_wanted = (int) v[2];
        } else if (key == "P" && n >= 3) {
          st.has_job = true;
          st.job_percent = (int) v[1];
          st.job_seconds = v[2] > 0 ? (uint32_t) v[2] : 0;
        } else if (key == "H" && n >= 1) {
          st.halt = (int) v[0];
        } else if (key == "C" && n >= 1) {
          st.model = (int) v[0];
        } else if (key == "S" && n >= 1) {
          st.spindle_rpm = (float) v[0];
        }
      }
    }
    start = bar + 1;
  }
  return true;
}

// "Makera_Z1_000000,192.168.1.50,2222,0,Idle"
struct Announcement {
  std::string name, ip;
  uint16_t port = 0;
  State state = State::UNKNOWN;
  bool has_state = false;
};

inline bool parse_announcement(const std::string &text, Announcement &a) {
  std::vector<std::string> f;
  size_t start = 0;
  while (true) {
    size_t comma = text.find(',', start);
    std::string field = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    while (!field.empty() && (field.back() == '\r' || field.back() == '\n' || field.back() == ' ' || field.back() == '\0'))
      field.pop_back();
    f.push_back(field);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (f.size() < 3 || f[0].empty() || f[1].empty()) return false;
  a = Announcement{};
  a.name = f[0];
  a.ip = f[1];
  a.port = (uint16_t) std::atoi(f[2].c_str());
  if (f.size() >= 5) {
    a.state = parse_state(f[4]);
    a.has_state = true;
  }
  return a.port != 0;
}

}  // namespace z1lib
