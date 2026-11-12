#pragma once
// What z1mba believes about the machine, from two sources:
//  - the UDP announcement (once a second and on every change, state only), and
//  - the TCP status reply (tool, job, halt), polled on a schedule.
// The tracker merges them and says when to poll next: right away when the UDP
// state changes, then fast during a tool change, slower during a job, slow
// when idle, and seldom when the machine is gone. Plain C++, no ESPHome, so
// tests/test_z1_tracker.cpp drives it with made-up times.
//
// Times are milliseconds from millis(); differences are unsigned so they
// survive the 49-day wrap.
#include <cstdint>
#include <string>

#include "z1_proto.h"

namespace z1lib {

struct TrackerConfig {
  uint32_t tool_poll_ms = 1000;      // in Tool, or a tool change on its way
  uint32_t job_poll_ms = 2000;       // a file is loaded
  uint32_t idle_poll_ms = 10000;
  uint32_t offline_poll_ms = 30000;  // no announcement and the last polls failed
  uint32_t announce_timeout_ms = 5000;
  uint8_t fails_offline = 3;         // failed polls in a row before "offline"
};

class Tracker {
 public:
  explicit Tracker(TrackerConfig cfg = {}) : cfg_(cfg) {}

  // A fixed address from the config. Without one, the first announcement sets it.
  void set_target(const std::string &ip, uint16_t port) {
    ip_ = ip;
    port_ = port;
    fixed_ = !ip.empty();
  }
  // Only take announcements from this machine name (empty: any Z1).
  void set_name_filter(const std::string &name) { name_filter_ = name; }

  // Returns false when the announcement was ignored (another machine).
  bool on_announcement(uint32_t now, const Announcement &a) {
    if (!name_filter_.empty() && a.name != name_filter_) return false;
    if (fixed_ && a.ip != ip_) return false;
    name_ = a.name;
    if (!fixed_) {
      ip_ = a.ip;
      port_ = a.port;
    }
    announced_ = true;
    last_announce_ = now;
    if (a.has_state) {
      if (!udp_known_ || a.state != udp_state_) {
        udp_state_ = a.state;
        udp_known_ = true;
        udp_changed_ = now;
        poll_now_ = true;  // the details (wanted tool, job) are one poll away
      }
    }
    return true;
  }

  void on_status(uint32_t now, const Status &st) {
    status_ = st;
    have_status_ = true;
    last_status_ = now;
    fails_ = 0;
  }

  void on_poll_failed(uint32_t /*now*/) {
    if (fails_ < 255) fails_++;
  }

  bool has_target() const { return !ip_.empty() && port_ != 0; }
  const std::string &ip() const { return ip_; }
  uint16_t port() const { return port_; }
  const std::string &name() const { return name_; }

  bool should_poll(uint32_t now) const {
    if (!has_target()) return false;
    if (!polled_) return true;
    if (poll_now_) return true;
    return now - last_poll_ >= interval(now);
  }
  void poll_started(uint32_t now) {
    polled_ = true;
    last_poll_ = now;
    poll_now_ = false;
  }

  uint32_t interval(uint32_t now) const {
    if (!online(now)) return cfg_.offline_poll_ms;
    const State s = state(now);
    if (s == State::TOOL || pending_tool(now) >= 0) return cfg_.tool_poll_ms;
    if (job_active(now) || s == State::RUN || s == State::HOME) return cfg_.job_poll_ms;
    return cfg_.idle_poll_ms;
  }

  bool online(uint32_t now) const {
    if (announced_ && now - last_announce_ < cfg_.announce_timeout_ms) return true;
    return have_status_ && fails_ < cfg_.fails_offline;
  }

  // The newer of the two sources wins.
  State state(uint32_t now) const {
    if (!online(now)) return State::UNKNOWN;
    const bool udp_fresh = udp_known_ && now - last_announce_ < cfg_.announce_timeout_ms;
    if (udp_fresh && (!have_status_ || (int32_t) (udp_changed_ - last_status_) > 0)) return udp_state_;
    if (have_status_) return status_.state;
    return udp_known_ ? udp_state_ : State::UNKNOWN;
  }

  // The tool in the spindle, -1 = none or measuring.
  int tool(uint32_t now) const { return online(now) && have_status_ && status_.has_tool ? status_.tool : -1; }
  float tool_length(uint32_t now) const {
    return online(now) && have_status_ ? status_.tool_length : NAN;
  }

  // The tool the machine wants next, or -1. The wanted field keeps its last
  // value after a change, so it only counts while it differs from the tool in
  // the spindle; -1 in the spindle means it is measuring the new bit, not
  // that another one is wanted (docs/Z1-PROTOCOL.md, "A tool change").
  int pending_tool(uint32_t now) const {
    if (!online(now) || !have_status_ || !status_.has_tool) return -1;
    const Status &s = status_;
    if (s.tool_wanted < 0 || s.tool < 0 || s.tool_wanted == s.tool) return -1;
    return s.tool_wanted;
  }

  bool tool_change(uint32_t now) const { return state(now) == State::TOOL; }
  bool job_active(uint32_t now) const { return online(now) && have_status_ && status_.has_job; }
  uint32_t job_seconds(uint32_t now) const { return job_active(now) ? status_.job_seconds : 0; }
  int job_percent(uint32_t now) const { return job_active(now) ? status_.job_percent : 0; }
  int halt(uint32_t now) const { return online(now) && have_status_ ? status_.halt : 0; }
  bool alarm(uint32_t now) const { return state(now) == State::ALARM || halt(now) > 0; }
  const Status &status() const { return status_; }

 private:
  TrackerConfig cfg_;
  std::string ip_, name_, name_filter_;
  uint16_t port_ = 0;
  bool fixed_ = false;

  bool announced_ = false, udp_known_ = false;
  uint32_t last_announce_ = 0, udp_changed_ = 0;
  State udp_state_ = State::UNKNOWN;

  bool have_status_ = false;
  uint32_t last_status_ = 0;
  Status status_;
  uint8_t fails_ = 0;

  bool polled_ = false, poll_now_ = false;
  uint32_t last_poll_ = 0;
};

}  // namespace z1lib
