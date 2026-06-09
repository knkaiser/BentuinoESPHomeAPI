// BinarySensor example: publish a toggling bool state.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-binary-sensor";
  cfg.friendly_name = "Binary Sensor Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
BinarySensor motion;

void setup() {
  Serial.begin(115200);
  delay(100);

  motion.set_object_id("motion");
  motion.set_name("Motion");
  motion.set_device_class("motion");

  api.add_binary_sensor(&motion);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  motion.publish_state(false);
  Serial.println("BinarySensor example ready");
}

void loop() {
  api.loop();
  static uint32_t last = 0;
  static bool state = false;
  uint32_t now = millis();
  if (now - last >= 3000) {
    last = now;
    state = !state;
    motion.publish_state(state);
    Serial.printf("motion -> %s\n", state ? "ON" : "OFF");
  }
}
