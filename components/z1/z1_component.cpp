#include "z1_component.h"

#include <cmath>

#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace z1 {

static const char *const TAG = "z1";

void Z1Component::setup() {}

void Z1Component::dump_config() {
  ESP_LOGCONFIG(TAG, "Makera Z1 (read-only status)");
  if (this->tracker_.has_target()) {
    ESP_LOGCONFIG(TAG, "  Host: %s:%u", this->tracker_.ip().c_str(), this->tracker_.port());
  } else {
    ESP_LOGCONFIG(TAG, "  Host: found from its UDP %u announcement", ::z1lib::DISCOVERY_PORT);
  }
}

void Z1Component::loop() {
  const uint32_t now = millis();
  if (!network::is_connected()) return;

  if (!this->udp_.is_open() && now - this->udp_retry_ >= 5000) {
    this->udp_retry_ = now;
    if (this->udp_.open(::z1lib::DISCOVERY_PORT)) {
      ESP_LOGD(TAG, "Listening for announcements on UDP %u", ::z1lib::DISCOVERY_PORT);
    } else {
      ESP_LOGW(TAG, "Could not open UDP %u", ::z1lib::DISCOVERY_PORT);
    }
  }
  this->udp_.poll([&](const std::string &text) {
    ::z1lib::Announcement a;
    if (!::z1lib::parse_announcement(text, a)) return;
    const bool first = this->tracker_.name().empty();
    if (this->tracker_.on_announcement(now, a) && first)
      ESP_LOGI(TAG, "Found %s at %s:%u", a.name.c_str(), a.ip.c_str(), a.port);
  });

  if (this->client_.busy()) {
    switch (this->client_.poll(now)) {
      case ::z1lib::StatusClient::Result::OK:
        this->tracker_.on_status(now, this->client_.status());
        this->last_error_.clear();
        break;
      case ::z1lib::StatusClient::Result::FAILED:
        this->tracker_.on_poll_failed(now);
        if (this->client_.error() != this->last_error_) {
          ESP_LOGW(TAG, "Status poll to %s failed: %s", this->tracker_.ip().c_str(), this->client_.error().c_str());
          this->last_error_ = this->client_.error();
        }
        break;
      default:
        break;
    }
  } else if (this->tracker_.should_poll(now)) {
    this->tracker_.poll_started(now);
    if (!this->client_.start(this->tracker_.ip(), this->tracker_.port(), now)) this->tracker_.on_poll_failed(now);
  }

  this->publish_(now);
}

static void pub(sensor::Sensor *s, float v) {
  if (s == nullptr) return;
  const float old = s->get_raw_state();
  if (std::isnan(v) && std::isnan(old)) return;
  if (s->has_state() && v == old) return;
  s->publish_state(v);
}
static void pub(text_sensor::TextSensor *s, const std::string &v) {
  if (s != nullptr && (!s->has_state() || s->get_raw_state() != v)) s->publish_state(v);
}
static void pub(binary_sensor::BinarySensor *s, bool v) {
  if (s != nullptr && (!s->has_state() || s->state != v)) s->publish_state(v);
}

void Z1Component::publish_(uint32_t now) {
  const auto &t = this->tracker_;
  const bool online = t.online(now);
  pub(this->online_bs_, online);
  pub(this->state_ts_, online ? ::z1lib::state_name(t.state(now)) : "Offline");
  pub(this->halt_ts_, ::z1lib::halt_reason(t.halt(now)));
  pub(this->ip_ts_, t.ip());
  const int tool = t.tool(now), next = t.pending_tool(now);
  pub(this->tool_s_, tool >= 0 ? (float) tool : NAN);
  pub(this->next_tool_s_, next >= 0 ? (float) next : NAN);
  pub(this->tool_length_s_, t.tool_length(now));
  pub(this->job_time_s_, t.job_active(now) ? (float) t.job_seconds(now) : NAN);
  pub(this->tool_change_bs_, t.tool_change(now));
  // One log line per change of what matters at the machine.
  const std::string summary = std::string(online ? ::z1lib::state_name(t.state(now)) : "Offline") +
                              " tool " + std::to_string(tool) + " next " + std::to_string(next) +
                              (t.job_active(now) ? " job" : "");
  if (summary != this->last_summary_) {
    ESP_LOGI(TAG, "%s", summary.c_str());
    this->last_summary_ = summary;
  }
  pub(this->job_active_bs_, t.job_active(now));
  pub(this->alarm_bs_, t.alarm(now));
}

}  // namespace z1
}  // namespace esphome
