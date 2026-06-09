// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Valve example: position + stop commands.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-valve";
  cfg.friendly_name = "Valve Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Valve water;
static float position = 0.0f;

void setup() {
  Serial.begin(115200);
  delay(100);

  water.set_object_id("water");
  water.set_name("Water Valve");
  water.set_supports_position(true);
  water.on_command = [](const ValveCommand &cmd) {
    if (cmd.stop) {
      Serial.println("valve stop");
      return;
    }
    if (cmd.has_position) position = cmd.position;
    water.publish_state(position);
    Serial.printf("valve position=%.2f\n", position);
  };

  api.add_valve(&water);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  water.publish_state(position);
  Serial.println("Valve example ready");
}

void loop() { api.loop(); }
