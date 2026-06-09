// Fan example: on/off, speed level, preset mode.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-fan";
  cfg.friendly_name = "Fan Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Fan fan;

void setup() {
  Serial.begin(115200);
  delay(100);

  fan.set_object_id("fan");
  fan.set_name("Fan");
  fan.set_supports_speed(true);
  fan.set_supported_speed_count(3);
  fan.set_supported_preset_modes({"auto", "sleep", "boost"});
  fan.on_command = [](const FanCommand &cmd) {
    static bool on = false;
    int32_t speed = 0;
    std::string preset;
    if (cmd.has_state) on = cmd.state;
    if (cmd.has_speed_level) speed = cmd.speed_level;
    if (cmd.has_preset_mode) preset = cmd.preset_mode;
    fan.publish_state(on, false, speed, 0, preset);
    Serial.printf("fan on=%d speed=%ld\n", on, (long)speed);
  };

  api.add_fan(&fan);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  fan.publish_state(false);
  Serial.println("Fan example ready");
}

void loop() { api.loop(); }
