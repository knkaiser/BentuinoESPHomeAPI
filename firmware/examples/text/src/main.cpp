// Text example: editable string field from Home Assistant.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-text";
  cfg.friendly_name = "Text Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Text label;

void setup() {
  Serial.begin(115200);
  delay(100);

  label.set_object_id("label");
  label.set_name("Label");
  label.set_max_length(64);
  label.on_control = [](const std::string &v) {
    Serial.printf("text -> %s\n", v.c_str());
  };

  api.add_text(&label);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  label.publish_state("hello");
  Serial.println("Text example ready");
}

void loop() { api.loop(); }
