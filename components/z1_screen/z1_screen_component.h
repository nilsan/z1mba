#pragma once
// ESPHome glue for the screen: z1_screen.h decides what to draw and when, this
// draws it on an ESPHome display and asks the display for each redraw.
#include <string>

#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "esphome/components/z1/z1_component.h"
#include "esphome/components/z1/z1_screen.h"
#include "esphome/core/component.h"

namespace esphome {
namespace z1_screen {

class Z1Screen : public Component {
 public:
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_z1(z1::Z1Component *z1) { this->z1_ = z1; }
  void set_display(display::Display *d);
  void set_fonts(font::Font *small, font::Font *medium, font::Font *big) {
    this->fonts_[0] = small;
    this->fonts_[1] = medium;
    this->fonts_[2] = big;
  }
  void set_refresh(uint32_t settle_ms, uint32_t calm_ms, int minute_step) {
    this->refresher_ = ::z1lib::Refresher(settle_ms, calm_ms);
    this->minute_step_ = minute_step;
  }

 protected:
  void draw_(display::Display &it);

  z1::Z1Component *z1_{nullptr};
  display::Display *display_{nullptr};
  font::Font *fonts_[3]{};
  ::z1lib::Refresher refresher_;
  int minute_step_{5};
  ::z1lib::View next_;
  bool requested_{false};
  uint32_t requested_at_{0}, last_check_{0};
};

}  // namespace z1_screen
}  // namespace esphome
