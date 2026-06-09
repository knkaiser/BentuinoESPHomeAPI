// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Number example: float slider with min/max/step.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-number";
  cfg.friendly_name = "Number Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Number level;

void setup() {
  Serial.begin(115200);
  delay(100);

  level.set_object_id("level");
  level.set_name("Level");
  level.set_min_value(0);
  level.set_max_value(100);
  level.set_step(1);
  level.on_control = [](float v) { Serial.printf("level -> %.0f\n", v); };

  api.add_number(&level);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  level.publish_state(50);
  Serial.println("Number example ready");
}

void loop() { api.loop(); }
