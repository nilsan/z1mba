#pragma once
// ESPHome glue for the z1 component: runs the UDP listener and the status
// client from loop(), feeds the tracker and publishes entities when a value
// changes. The logic lives in the plain headers next to this file.
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include "z1_client.h"
#include "z1_tracker.h"

namespace esphome {
namespace z1 {

class Z1Component : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_host(const std::string &host, uint16_t port) { tracker_.set_target(host, port); }
  void set_machine_name(const std::string &name) { tracker_.set_name_filter(name); }

  void set_state_text_sensor(text_sensor::TextSensor *s) { state_ts_ = s; }
  void set_halt_reason_text_sensor(text_sensor::TextSensor *s) { halt_ts_ = s; }
  void set_machine_ip_text_sensor(text_sensor::TextSensor *s) { ip_ts_ = s; }
  void set_tool_sensor(sensor::Sensor *s) { tool_s_ = s; }
  void set_next_tool_sensor(sensor::Sensor *s) { next_tool_s_ = s; }
  void set_tool_length_sensor(sensor::Sensor *s) { tool_length_s_ = s; }
  void set_job_time_sensor(sensor::Sensor *s) { job_time_s_ = s; }
  void set_online_binary_sensor(binary_sensor::BinarySensor *s) { online_bs_ = s; }
  void set_tool_change_binary_sensor(binary_sensor::BinarySensor *s) { tool_change_bs_ = s; }
  void set_job_active_binary_sensor(binary_sensor::BinarySensor *s) { job_active_bs_ = s; }
  void set_alarm_binary_sensor(binary_sensor::BinarySensor *s) { alarm_bs_ = s; }

  // For display lambdas: the merged view of the machine right now.
  const ::z1lib::Tracker &tracker() const { return tracker_; }

 protected:
  void publish_(uint32_t now);

  ::z1lib::Tracker tracker_;
  ::z1lib::UdpListener udp_;
  ::z1lib::StatusClient client_;
  uint32_t udp_retry_ = 0;
  std::string last_error_, last_summary_;

  text_sensor::TextSensor *state_ts_{nullptr}, *halt_ts_{nullptr}, *ip_ts_{nullptr};
  sensor::Sensor *tool_s_{nullptr}, *next_tool_s_{nullptr}, *tool_length_s_{nullptr}, *job_time_s_{nullptr};
  binary_sensor::BinarySensor *online_bs_{nullptr}, *tool_change_bs_{nullptr}, *job_active_bs_{nullptr},
      *alarm_bs_{nullptr};
};

}  // namespace z1
}  // namespace esphome
