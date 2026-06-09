// Event example: push event notifications (no persistent state).
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-event";
  cfg.friendly_name = "Event Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Event doorbell;

void setup() {
  Serial.begin(115200);
  delay(100);

  doorbell.set_object_id("doorbell");
  doorbell.set_name("Doorbell");
  doorbell.set_event_types({"pressed", "long_press"});

  api.add_event(&doorbell);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  Serial.println("Event example ready");
}

void loop() {
  api.loop();
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last >= 10000) {
    last = now;
    doorbell.publish_event("pressed");
    Serial.println("event: pressed");
  }
}
