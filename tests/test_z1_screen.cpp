// Native tests for z1_screen.h: what each state says, that the layout stays on
// the screen at every size, and when the screen is redrawn.
//
//   test_z1_screen [name W H small medium big]... [--svg DIR]
//
// tests/run.sh passes the presets in screens/*.yaml, and a few other e-paper
// sizes are built in; with --svg it also writes a preview of each screen and
// state, to look at in a browser.
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "../components/z1/z1_screen.h"
#include "check.h"

using namespace z1lib;

// Font sizes of one screen, in px as given to ESPHome's font:.
struct Preset {
  std::string name;
  int w, h, small, medium, big;
};

// Records what draw() does. Text is measured like a condensed bold face
// (Roboto Condensed: about 0.5 em per character, 1.17 em line height).
struct Canvas {
  struct Op {
    char kind;  // f fill, b box, v vline, t text
    Box box;
    Ink ink;
    Size size;
    std::string text;
  };
  Preset p;
  std::vector<Op> ops;

  int width() { return p.w; }
  int height() { return p.h; }
  int px(Size s) const { return s == Size::SMALL ? p.small : s == Size::MEDIUM ? p.medium : p.big; }
  int text_height(Size s) { return px(s) * 117 / 100; }
  int text_width(Size s, const std::string &t) { return static_cast<int>(t.size()) * px(s) / 2; }
  void fill(Ink i) { ops.push_back({'f', {0, 0, p.w, p.h}, i, Size::SMALL, ""}); }
  void box(Box b, Ink i) { ops.push_back({'b', b, i, Size::SMALL, ""}); }
  void vline(int x, int y, int h, Ink i) { ops.push_back({'v', {x, y, 1, h}, i, Size::SMALL, ""}); }
  void text(int x, int y, Size s, Ink i, Align a, const std::string &t) {
    const int w = text_width(s, t);
    const int x0 = a == Align::LEFT ? x : a == Align::RIGHT ? x - w : x - w / 2;
    ops.push_back({'t', {x0, y, w, text_height(s)}, i, s, t});
  }

  bool on_screen(const Box &b) const { return b.x >= 0 && b.y >= 0 && b.x + b.w <= p.w && b.y + b.h <= p.h; }
  std::vector<const Op *> texts() const {
    std::vector<const Op *> out;
    for (const auto &o : ops)
      if (o.kind == 't') out.push_back(&o);
    return out;
  }
  const Op *find(const std::string &t) const {
    for (const auto &o : ops)
      if (o.kind == 't' && o.text == t) return &o;
    return nullptr;
  }

  std::string svg() const {
    auto colour = [](Ink i) { return i == Ink::PAPER ? "#fff" : i == Ink::INK ? "#111" : "#c22"; };
    std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(p.w * 2) +
                    "\" height=\"" + std::to_string(p.h * 2) + "\" viewBox=\"0 0 " + std::to_string(p.w) + " " +
                    std::to_string(p.h) + "\" style=\"border:1px solid #999\">\n";
    for (const auto &o : ops) {
      if (o.kind == 't') {
        s += "<text x=\"" + std::to_string(o.box.x) + "\" y=\"" + std::to_string(o.box.y + px(o.size)) +
             "\" font-family=\"Roboto Condensed,Arial Narrow,sans-serif\" font-weight=\"700\" font-size=\"" +
             std::to_string(px(o.size)) + "\" textLength=\"" + std::to_string(o.box.w) + "\" fill=\"" +
             colour(o.ink) + "\">" + o.text + "</text>\n";
      } else {
        s += "<rect x=\"" + std::to_string(o.box.x) + "\" y=\"" + std::to_string(o.box.y) + "\" width=\"" +
             std::to_string(o.box.w) + "\" height=\"" + std::to_string(o.box.h) + "\" fill=\"" + colour(o.ink) +
             "\"/>\n";
      }
    }
    return s + "</svg>\n";
  }
};

static bool overlap(const Box &a, const Box &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static View view(Mode m, int tool, int wanted = -1, int minutes = -1, int halt = 0) {
  View v;
  v.mode = m;
  v.state = m == Mode::TOOL_CHANGE ? State::TOOL : m == Mode::JOB ? State::RUN : State::IDLE;
  v.tool = tool;
  v.wanted = wanted;
  v.job_minutes = minutes;
  v.halt = halt;
  return v;
}

static const std::vector<std::pair<std::string, View>> &states() {
  static const std::vector<std::pair<std::string, View>> s = {
      {"offline", View{}},
      {"idle", view(Mode::IDLE, 4)},
      {"job", view(Mode::JOB, 4, -1, 25)},
      {"tool-change", view(Mode::TOOL_CHANGE, 2, 4, 40)},
      {"tool-change-measuring", view(Mode::TOOL_CHANGE, -1, 12, 40)},
      {"probe", view(Mode::TOOL_CHANGE, 4, 9999, 5)},
      {"alarm", view(Mode::ALARM, 4, -1, 10, 2)},
  };
  return s;
}

static void test_tool_label() {
  CHECK(tool_label(-1) == "-");
  CHECK(tool_label(4) == "T4");
  CHECK(tool_label(12) == "T12");
  CHECK(tool_label(0) == "Probe" && tool_label(9999) == "Probe");
  CHECK(tool_label(8888) == "Laser");
}

static void test_compose() {
  Content c = compose(View{});
  CHECK(c.logo && c.lines[0] == "Z1 offline" && c.hero.empty());

  c = compose(view(Mode::TOOL_CHANGE, 2, 4, 40));
  CHECK(c.title == "Tool change" && c.hero == "T4" && c.hero_alert);
  CHECK(c.lines[0] == "Load this bit" && c.lines[1] == "Out: T2");
  CHECK(c.corner == "40 min");

  c = compose(view(Mode::TOOL_CHANGE, -1, 4));
  CHECK(c.hero == "T4" && c.lines[1] == "Measuring" && c.corner.empty());

  c = compose(view(Mode::TOOL_CHANGE, 2, -1));  // wanted not known yet
  CHECK(c.hero == "-" && c.lines[0] == "Load the next bit");

  c = compose(view(Mode::IDLE, 4));
  CHECK(c.title == "Idle" && c.hero == "T4" && !c.hero_alert);

  c = compose(view(Mode::JOB, -1, -1, 0));
  CHECK(c.title == "Running" && c.lines[0] == "No bit" && c.corner == "0 min");

  c = compose(view(Mode::ALARM, 4, -1, -1, 1));
  CHECK(c.hero == "!" && c.hero_alert && c.lines[0] == "Stopped by hand");
}

static Status status(const char *text) {
  Status s;
  parse_status(text, s);
  return s;
}

static void test_make_view() {
  Tracker t;
  CHECK(make_view(t, 0).mode == Mode::OFFLINE);
  Announcement a;
  parse_announcement("Makera_Z1_000000,192.168.1.50,2222,0,Idle", a);
  t.on_announcement(0, a);
  t.on_status(10, status("<Idle|T:4,1.423,-1>"));
  View v = make_view(t, 20);
  CHECK(v.mode == Mode::IDLE && v.tool == 4 && v.job_minutes == -1);

  t.on_status(30, status("<Run|T:4,1.423,-1|P:0,58,779>"));  // 12 min 59 s
  v = make_view(t, 40);
  CHECK(v.mode == Mode::JOB && v.job_minutes == 10);
  CHECK(make_view(t, 40, 1).job_minutes == 12);

  t.on_status(50, status("<Tool|T:4,1.423,2|P:0,60,800>"));
  v = make_view(t, 60);
  CHECK(v.mode == Mode::TOOL_CHANGE && v.tool == 4 && v.wanted == 2);
  t.on_status(70, status("<Tool|T:-1,0,2|P:0,60,800>"));  // measuring the new bit
  v = make_view(t, 80);
  CHECK(v.mode == Mode::TOOL_CHANGE && v.tool == -1 && v.wanted == 2);

  t.on_status(90, status("<Alarm|T:2,1.1,2|P:0,60,800|H:1>"));
  CHECK(make_view(t, 100).mode == Mode::ALARM && make_view(t, 100).halt == 1);
}

static void test_refresher() {
  Refresher r(1500, 300000);
  const View idle = view(Mode::IDLE, 4);
  CHECK(r.due(idle, 0));  // the first view is always drawn
  r.drawn(idle, 0);
  CHECK(!r.due(idle, 100));

  // A tool change is drawn once it has held for the settle time.
  const View change = view(Mode::TOOL_CHANGE, 4, 2);
  CHECK(!r.due(change, 1000));
  CHECK(!r.due(change, 2000));
  CHECK(r.due(change, 2500));
  r.drawn(change, 2500);

  // A flicker through another state and back is not drawn.
  CHECK(!r.due(view(Mode::JOB, 4, -1, 0), 3000));
  CHECK(!r.due(change, 3500));
  CHECK(!r.due(view(Mode::JOB, 4, -1, 0), 4000));
  CHECK(!r.due(view(Mode::JOB, 4, -1, 0), 5400));  // settle restarts after the flicker
  CHECK(r.due(view(Mode::JOB, 4, -1, 0), 5500));
  r.drawn(view(Mode::JOB, 4, -1, 0), 5500);

  // The job clock only waits for the calm interval.
  CHECK(!r.due(view(Mode::JOB, 4, -1, 5), 10000));
  CHECK(!r.due(view(Mode::JOB, 4, -1, 5), 300000));
  CHECK(r.due(view(Mode::JOB, 4, -1, 5), 305500));

  // millis() wrapping round does not stall it.
  Refresher w(1500, 300000);
  w.drawn(idle, 0xFFFFF000u);
  CHECK(!w.due(change, 0xFFFFFF00u));
  CHECK(w.due(change, 0x00000800u));
}

// Every state on one screen: nothing off the edge, no text on top of other
// text, and the important words are there in full.
static void test_layout(const Preset &p, const char *svg_dir) {
  for (const auto &[name, v] : states()) {
    Canvas c{p, {}};
    const Content k = compose(v);
    draw(c, k);
    const auto texts = c.texts();
    bool ok = true;
    for (const auto &o : c.ops) {
      if (!c.on_screen(o.box)) {
        std::printf("  %s %s: '%c' %s at %d,%d %dx%d is off the %dx%d screen\n", p.name.c_str(), name.c_str(),
                    o.kind, o.text.c_str(), o.box.x, o.box.y, o.box.w, o.box.h, p.w, p.h);
        ok = false;
      }
    }
    for (size_t i = 0; i < texts.size(); i++)
      for (size_t j = i + 1; j < texts.size(); j++)
        if (overlap(texts[i]->box, texts[j]->box)) {
          std::printf("  %s %s: '%s' overlaps '%s'\n", p.name.c_str(), name.c_str(), texts[i]->text.c_str(),
                      texts[j]->text.c_str());
          ok = false;
        }
    CHECK(ok);
    if (!k.hero.empty()) {
      const auto *hero = c.find(k.hero);
      CHECK(hero != nullptr);
      if (hero && k.hero.size() <= 3) CHECK(hero->size == Size::BIG);  // "T12" fits big everywhere
    }
    CHECK(k.lines[0].empty() || c.find(k.lines[0]) != nullptr);
    if (k.logo) {
      int columns = 0;
      for (const auto &o : c.ops) columns += o.kind == 'v';
      CHECK(columns >= std::min(p.w, p.h) / 2);
    }
    if (svg_dir) {
      std::ofstream(std::string(svg_dir) + "/" + p.name + "-" + name + ".svg") << c.svg();
    }
  }
}

int main(int argc, char **argv) {
  std::vector<Preset> presets;
  const char *svg_dir = nullptr;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--svg" && i + 1 < argc) {
      svg_dir = argv[++i];
    } else if (i + 5 < argc) {
      presets.push_back({argv[i], std::atoi(argv[i + 1]), std::atoi(argv[i + 2]), std::atoi(argv[i + 3]),
                         std::atoi(argv[i + 4]), std::atoi(argv[i + 5])});
      i += 5;
    }
  }
  if (presets.empty()) presets.push_back({"weact-2.9in-3c", 296, 128, 14, 20, 64});
  // Only the 2.9" ships as a preset, but the layout should hold at other common
  // e-paper sizes too: 2.13", 1.54" (square) and 4.2".
  presets.push_back({"other-250x122", 250, 122, 12, 18, 60});
  presets.push_back({"other-200x200", 200, 200, 14, 20, 64});
  presets.push_back({"other-400x300", 400, 300, 20, 28, 110});

  test_tool_label();
  test_compose();
  test_make_view();
  test_refresher();
  for (const auto &p : presets) test_layout(p, svg_dir);
  std::printf("%zu screen size(s)\n", presets.size());
  DONE("z1_screen");
}
