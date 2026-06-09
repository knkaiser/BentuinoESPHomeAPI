// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// TextSensor example: publish a string state.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-text-sensor";
  cfg.friendly_name = "Text Sensor Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
TextSensor status;

void setup() {
  Serial.begin(115200);
  delay(100);

  status.set_object_id("status");
  status.set_name("Status");

  api.add_text_sensor(&status);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  status.publish_state("idle");
  Serial.println("TextSensor example ready");
}

void loop() {
  api.loop();
  static uint32_t last = 0;
  static uint32_t tick = 0;
  uint32_t now = millis();
  if (now - last >= 3000) {
    last = now;
    tick++;
    status.publish_state("tick " + std::to_string(tick));
  }
}
