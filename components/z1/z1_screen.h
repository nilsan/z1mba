#pragma once
// What the screen shows, where, and when to redraw it, for a screen of any
// size. Plain C++ like the other headers here: it draws through a Canvas (see
// draw() below), which is ESPHome's display on the device (components/z1_screen)
// and a recorder in the native tests.
//
//   View      the few facts the screen shows, taken from the Tracker
//   Content   the words for a View: header, the big "hero" text, detail lines
//   draw()    lays Content out on the canvas from its width and height
//   Refresher decides when a changed View is worth an e-paper refresh
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "z1_logo.h"
#include "z1_tracker.h"

namespace z1lib {

enum class Mode : uint8_t { OFFLINE, IDLE, JOB, TOOL_CHANGE, ALARM };

struct View {
  Mode mode = Mode::OFFLINE;
  State state = State::UNKNOWN;
  int tool = -1;         // in the spindle; -1 none, or measuring the new one
  int wanted = -1;       // the bit to load, at a tool change
  int job_minutes = -1;  // rounded down to the step; -1 without a job
  int halt = 0;

  bool operator==(const View &o) const {
    return mode == o.mode && state == o.state && tool == o.tool && wanted == o.wanted &&
           job_minutes == o.job_minutes && halt == o.halt;
  }
  bool operator!=(const View &o) const { return !(*this == o); }
};

// minute_step: the job clock moves in steps this size, so a running job costs
// a refresh every few minutes, not every minute.
inline View make_view(const Tracker &t, uint32_t now, int minute_step = 5) {
  View v;
  if (!t.online(now)) return v;
  v.state = t.state(now);
  v.tool = t.tool(now);
  v.halt = t.halt(now);
  if (t.job_active(now)) {
    const int step = std::max(1, minute_step);
    v.job_minutes = static_cast<int>(t.job_seconds(now) / 60) / step * step;
  }
  if (t.alarm(now)) {
    v.mode = Mode::ALARM;
  } else if (t.tool_change(now)) {
    v.mode = Mode::TOOL_CHANGE;
    // The wanted field, not pending_tool(): while the new bit is measured the
    // spindle reads -1 and the screen should keep showing which bit it is.
    v.wanted = t.status().tool_wanted;
  } else {
    v.mode = t.job_active(now) ? Mode::JOB : Mode::IDLE;
  }
  return v;
}

// Makera's special tool numbers: 0 and 9999 are probes, 8888 the laser.
inline std::string tool_label(int tool) {
  if (tool < 0) return "-";
  if (tool == 0 || tool == 9999) return "Probe";
  if (tool == 8888) return "Laser";
  char buf[16];
  std::snprintf(buf, sizeof buf, "T%d", tool);
  return buf;
}

enum class Ink : uint8_t { PAPER, INK, ACCENT };  // white, black, red (black on two-colour screens)
enum class Size : uint8_t { SMALL, MEDIUM, BIG };
enum class Align : uint8_t { LEFT, CENTER, RIGHT };

struct Content {
  std::string title;   // header, left
  std::string corner;  // header, right
  std::string hero;    // the one thing to read from across the room
  bool hero_alert = false;  // hero in paper on an accent block: act now
  std::string lines[3];
  bool logo = false;
};

inline Content compose(const View &v) {
  Content c;
  if (v.job_minutes >= 0) c.corner = std::to_string(v.job_minutes) + " min";
  switch (v.mode) {
    case Mode::OFFLINE:
      c.title = "z1mba";
      c.lines[0] = "Z1 offline";
      c.logo = true;
      break;
    case Mode::ALARM:
      c.title = "Alarm";
      c.hero = "!";
      c.hero_alert = true;
      c.lines[0] = v.halt > 0 ? halt_reason(v.halt) : "Alarm";
      c.lines[1] = "Check the machine";
      break;
    case Mode::TOOL_CHANGE:
      c.title = "Tool change";
      c.hero = tool_label(v.wanted);
      c.hero_alert = true;
      c.lines[0] = v.wanted >= 0 ? "Load this bit" : "Load the next bit";
      if (v.tool >= 0 && v.tool != v.wanted)
        c.lines[1] = "Out: " + tool_label(v.tool);
      else if (v.tool < 0)
        c.lines[1] = "Measuring";
      c.lines[2] = "then press the button";
      break;
    case Mode::JOB:
    case Mode::IDLE:
      c.title = state_name(v.state);
      c.hero = tool_label(v.tool);
      c.lines[0] = v.tool >= 0 ? "in the spindle" : "No bit";
      break;
  }
  return c;
}

// A box on the canvas; the tests check that every one stays on the screen.
struct Box {
  int x, y, w, h;
};

// The Canvas draw() needs:
//   int width(); int height();
//   void fill(Ink);  void box(Box, Ink);  void vline(int x, int y, int h, Ink);
//   int text_width(Size, const std::string &);  int text_height(Size);
//   void text(int x, int y_top, Size, Ink, Align, const std::string &);
namespace detail {

// The largest size, from `from` down, that fits the box; shortened with ".."
// when even the smallest does not.
template<class C> std::string fit(C &c, const std::string &s, int max_w, int max_h, Size from, Size &out) {
  for (int i = static_cast<int>(from); i >= 0; i--) {
    out = static_cast<Size>(i);
    if (c.text_width(out, s) <= max_w && c.text_height(out) <= max_h) return s;
  }
  out = Size::SMALL;
  if (c.text_height(out) > max_h) return "";
  std::string t = s;
  while (!t.empty() && c.text_width(out, t + "..") > max_w) t.pop_back();
  return t.empty() ? "" : t + "..";
}

template<class C> void text_in(C &c, Box b, Size from, Ink ink, Align a, const std::string &s) {
  if (s.empty() || b.w <= 0 || b.h <= 0) return;
  Size size;
  const std::string t = fit(c, s, b.w, b.h, from, size);
  if (t.empty()) return;
  const int x = a == Align::LEFT ? b.x : a == Align::RIGHT ? b.x + b.w : b.x + b.w / 2;
  c.text(x, b.y + (b.h - c.text_height(size)) / 2, size, ink, a, t);
}

// The skyline from z1_logo.h, filled down to the bottom of the box.
template<class C> void logo(C &c, Box b, Ink ink) {
  if (b.w <= 0 || b.h <= 0) return;
  // Keep the mark's proportions inside the box.
  int w = b.w, h = b.w * LOGO_HEIGHT / LOGO_WIDTH;
  if (h > b.h) {
    h = b.h;
    w = b.h * LOGO_WIDTH / LOGO_HEIGHT;
  }
  const int x0 = b.x + (b.w - w) / 2, y0 = b.y + b.h - h;
  size_t seg = 0;
  for (int px = 0; px < w; px++) {
    const float fx = (px + 0.5f) * LOGO_WIDTH / w;
    while (seg + 2 < LOGO_POINTS && LOGO[seg + 1][0] < fx) seg++;
    const float ax = LOGO[seg][0], ay = LOGO[seg][1], bx = LOGO[seg + 1][0], by = LOGO[seg + 1][1];
    const float fy = bx > ax ? ay + (by - ay) * std::clamp((fx - ax) / (bx - ax), 0.0f, 1.0f) : ay;
    const int top = std::clamp(static_cast<int>(fy * h / LOGO_HEIGHT), 0, h - 1);
    c.vline(x0 + px, y0 + top, h - top, ink);
  }
}

}  // namespace detail

template<class C> void draw(C &c, const Content &k) {
  const int W = c.width(), H = c.height();
  const int pad = std::max(2, std::min(W, H) / 40);
  c.fill(Ink::PAPER);

  // Header: title left, job clock right, a rule under both.
  const int head_h = c.text_height(Size::SMALL) + 2 * pad;
  const int corner_w = k.corner.empty() ? 0 : std::min(W / 2, c.text_width(Size::SMALL, k.corner));
  detail::text_in(c, {pad, 0, W - 3 * pad - corner_w, head_h}, Size::SMALL, Ink::INK, Align::LEFT, k.title);
  detail::text_in(c, {W - pad - corner_w, 0, corner_w, head_h}, Size::SMALL, Ink::INK, Align::RIGHT, k.corner);
  const int rule = std::max(1, pad / 2);
  c.box({0, head_h, W, rule}, Ink::INK);

  const Box main{0, head_h + rule, W, H - head_h - rule};
  int n = 0;
  for (const auto &l : k.lines) n += !l.empty();
  const int line_h = c.text_height(Size::MEDIUM) + pad;
  // Hero (or, offline, the mountain) beside the lines on a wide screen, above
  // them on a tall or square one, or when a long hero ("Probe") would only fit
  // beside them in a smaller font.
  const bool wide =
      W * 10 >= H * 13 && (k.logo || c.text_width(Size::BIG, k.hero) + 4 * pad <= W * 3 / 5);
  Box hero, rest;
  if (wide) {
    const int want = k.logo ? W / 2 : c.text_width(Size::BIG, k.hero) + 4 * pad;
    const int hw = std::clamp(want, W * 2 / 5, W * 3 / 5);
    hero = {pad, main.y + pad, hw - pad, main.h - 2 * pad};
    rest = {hw + pad, main.y + pad, W - hw - 2 * pad, main.h - 2 * pad};
  } else {
    const int hh = k.logo ? main.h * 3 / 5 : std::min(c.text_height(Size::BIG) + 2 * pad, main.h * 3 / 5);
    hero = {pad, main.y + pad, W - 2 * pad, hh};
    rest = {pad, hero.y + hh + pad, W - 2 * pad, main.y + main.h - (hero.y + hh + 2 * pad)};
  }
  if (k.hero_alert) c.box(hero, Ink::ACCENT);
  if (k.logo)
    detail::logo(c, {hero.x + pad, hero.y + pad, hero.w - 2 * pad, hero.h - 2 * pad}, Ink::INK);
  else
    detail::text_in(c, {hero.x + pad, hero.y + pad, hero.w - 2 * pad, hero.h - 2 * pad}, Size::BIG,
                  k.hero_alert ? Ink::PAPER : Ink::INK, Align::CENTER, k.hero);

  // The lines, centred as a block in what is left.
  const int rows = std::max(1, std::min(n, rest.h / std::max(1, line_h)));
  int y = rest.y + (rest.h - rows * line_h) / 2;
  for (int i = 0, row = 0; i < 3 && row < rows; i++) {
    if (k.lines[i].empty()) continue;
    detail::text_in(c, {rest.x, y, rest.w, line_h}, Size::MEDIUM, i == 0 && k.hero_alert ? Ink::ACCENT : Ink::INK,
                    wide ? Align::LEFT : Align::CENTER, k.lines[i]);
    y += line_h;
    row++;
  }
}

// When to redraw. An e-paper refresh takes seconds and flashes, so a change
// that matters (mode, bit, alarm) is drawn as soon as it has held still for
// `settle_ms` (the UDP state can flicker through a state for a moment), and
// anything else, like the job clock, at most every `calm_ms`.
class Refresher {
 public:
  Refresher(uint32_t settle_ms = 1500, uint32_t calm_ms = 300000) : settle_ms_(settle_ms), calm_ms_(calm_ms) {}

  static bool matters(const View &a, const View &b) {
    return a.mode != b.mode || a.tool != b.tool || a.wanted != b.wanted || a.halt != b.halt;
  }

  bool due(const View &v, uint32_t now) {
    if (!drawn_) return true;
    if (v == shown_) {
      seen_ = false;
      return false;
    }
    if (!seen_ || v != seen_view_) {
      seen_view_ = v;
      seen_since_ = now;
      seen_ = true;
    }
    if (now - seen_since_ < settle_ms_) return false;
    return matters(shown_, v) || now - last_ >= calm_ms_;
  }

  void drawn(const View &v, uint32_t now) {
    shown_ = v;
    last_ = now;
    drawn_ = true;
    seen_ = false;
  }

  const View &shown() const { return shown_; }

 private:
  uint32_t settle_ms_, calm_ms_;
  View shown_, seen_view_;
  uint32_t last_ = 0, seen_since_ = 0;
  bool drawn_ = false, seen_ = false;
};

}  // namespace z1lib
