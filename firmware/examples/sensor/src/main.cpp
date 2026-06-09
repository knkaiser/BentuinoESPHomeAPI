// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Sensor example: publish a float reading (uptime in seconds).
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-sensor";
  cfg.friendly_name = "Sensor Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Sensor uptime;

void setup() {
  Serial.begin(115200);
  delay(100);

  uptime.set_object_id("uptime");
  uptime.set_name("Uptime");
  uptime.set_unit_of_measurement("s");
  uptime.set_accuracy_decimals(0);
  uptime.set_device_class("duration");

  api.add_sensor(&uptime);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  uptime.publish_state(0);
  Serial.println("Sensor example ready");
}

void loop() {
  api.loop();
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last >= 2000) {
    last = now;
    uptime.publish_state(now / 1000.0f);
  }
}
