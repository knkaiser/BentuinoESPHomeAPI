// Date example: year / month / day picker.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-date";
  cfg.friendly_name = "Date Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Date reminder;

void setup() {
  Serial.begin(115200);
  delay(100);

  reminder.set_object_id("reminder");
  reminder.set_name("Reminder Date");
  reminder.on_control = [](uint32_t y, uint32_t m, uint32_t d) {
    Serial.printf("date -> %04u-%02u-%02u\n", y, m, d);
  };

  api.add_date(&reminder);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  reminder.publish_state(2026, 6, 9);
  Serial.println("Date example ready");
}

void loop() { api.loop(); }
