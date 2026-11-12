#include "z1_screen_component.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace z1_screen {

static const char *const TAG = "z1_screen";

// A redraw the display never got to (it was still busy with the last one)
// is asked for again after this long.
static const uint32_t REQUEST_TIMEOUT_MS = 60000;

namespace {

// z1_screen.h's Canvas on an ESPHome display. Red is the accent; a
// two-colour e-paper turns it into black by itself.
struct Canvas {
  display::Display &it;
  font::Font *const *fonts;

  static Color colour(::z1lib::Ink i) {
    switch (i) {
      case ::z1lib::Ink::PAPER: return Color(255, 255, 255);
      case ::z1lib::Ink::ACCENT: return Color(255, 0, 0);
      default: return Color(0, 0, 0);
    }
  }
  font::Font *font(::z1lib::Size s) const { return fonts[static_cast<int>(s)]; }

  int width() { return it.get_width(); }
  int height() { return it.get_height(); }
  void fill(::z1lib::Ink i) { it.fill(colour(i)); }
  void box(::z1lib::Box b, ::z1lib::Ink i) { it.filled_rectangle(b.x, b.y, b.w, b.h, colour(i)); }
  void vline(int x, int y, int h, ::z1lib::Ink i) { it.vertical_line(x, y, h, colour(i)); }
  int text_height(::z1lib::Size s) { return font(s)->get_height(); }
  int text_width(::z1lib::Size s, const std::string &t) {
    int w = 0, x_offset = 0, baseline = 0, h = 0;
    font(s)->measure(t.c_str(), &w, &x_offset, &baseline, &h);
    return w;
  }
  void text(int x, int y, ::z1lib::Size s, ::z1lib::Ink i, ::z1lib::Align a, const std::string &t) {
    const auto align = a == ::z1lib::Align::LEFT    ? display::TextAlign::TOP_LEFT
                       : a == ::z1lib::Align::RIGHT ? display::TextAlign::TOP_RIGHT
                                                    : display::TextAlign::TOP_CENTER;
    it.print(x, y, font(s), colour(i), align, t.c_str());
  }
};

}  // namespace

void Z1Screen::set_display(display::Display *d) {
  this->display_ = d;
  d->set_writer([this](display::Display &it) { this->draw_(it); });
}

void Z1Screen::loop() {
  const uint32_t now = millis();
  if (now - this->last_check_ < 250) return;
  this->last_check_ = now;
  if (this->requested_ && now - this->requested_at_ < REQUEST_TIMEOUT_MS) return;

  const ::z1lib::View v = ::z1lib::make_view(this->z1_->tracker(), now, this->minute_step_);
  if (!this->refresher_.due(v, now)) return;
  this->next_ = v;
  this->requested_ = true;
  this->requested_at_ = now;
  this->display_->update();
}

void Z1Screen::draw_(display::Display &it) {
  // Draws what loop() asked for, or the current view when the display redraws
  // by itself (at boot).
  const uint32_t now = millis();
  const ::z1lib::View v =
      this->requested_ ? this->next_ : ::z1lib::make_view(this->z1_->tracker(), now, this->minute_step_);
  Canvas c{it, this->fonts_};
  ::z1lib::draw(c, ::z1lib::compose(v));
  this->refresher_.drawn(v, now);
  this->requested_ = false;
  ESP_LOGD(TAG, "Drew %dx%d", it.get_width(), it.get_height());
}

void Z1Screen::dump_config() {
  ESP_LOGCONFIG(TAG, "z1 screen:");
  if (this->display_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Size: %dx%d", this->display_->get_width(), this->display_->get_height());
  }
}

}  // namespace z1_screen
}  // namespace esphome
