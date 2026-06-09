// Update example: firmware update install simulation.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-update";
  cfg.friendly_name = "Update Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Update firmware;

void setup() {
  Serial.begin(115200);
  delay(100);

  firmware.set_object_id("firmware");
  firmware.set_name("Firmware");
  firmware.set_device_class("firmware");
  firmware.on_command = [](uint32_t command) {
    if (command != 0) return;
    Serial.println("update install started");
    firmware.publish_state("1.0.0", "1.1.0", true, 0.0f);
    firmware.publish_state("1.0.0", "1.1.0", true, 0.5f);
    firmware.publish_state("1.1.0", "1.1.0", false, 1.0f);
    Serial.println("update install complete");
  };

  api.add_update(&firmware);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  firmware.publish_state("1.0.0", "1.1.0");
  Serial.println("Update example ready");
}

void loop() { api.loop(); }
