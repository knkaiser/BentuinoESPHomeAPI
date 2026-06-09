// Select example: choose from a fixed option list.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-select";
  cfg.friendly_name = "Select Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Select mode;

void setup() {
  Serial.begin(115200);
  delay(100);

  mode.set_object_id("mode");
  mode.set_name("Mode");
  mode.set_options({"low", "medium", "high"});
  mode.on_control = [](const std::string &v) {
    Serial.printf("mode -> %s\n", v.c_str());
  };

  api.add_select(&mode);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  mode.publish_state("medium");
  Serial.println("Select example ready");
}

void loop() { api.loop(); }
