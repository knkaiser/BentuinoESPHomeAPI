// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Climate example: mode + target temperature.
#include <Arduino.h>
#include <math.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-climate";
  cfg.friendly_name = "Climate Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Climate thermostat;
static uint32_t mode = 3;  // HEAT
static float target = 21.0f;

void setup() {
  Serial.begin(115200);
  delay(100);

  thermostat.set_object_id("thermostat");
  thermostat.set_name("Thermostat");
  thermostat.set_supported_modes({0, 3});  // OFF, HEAT
  thermostat.on_command = [](const ClimateCommand &cmd) {
    if (cmd.has_mode) mode = cmd.mode;
    if (cmd.has_target_temperature) target = cmd.target_temperature;
    float current = 20.0f + sinf(millis() / 10000.0f);
    thermostat.publish_state(mode, current, target);
    Serial.printf("climate mode=%u target=%.1f\n", mode, target);
  };

  api.add_climate(&thermostat);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  thermostat.publish_state(mode, 21.0f, target);
  Serial.println("Climate example ready");
}

void loop() {
  api.loop();
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last >= 5000) {
    last = now;
    float current = 20.0f + sinf(now / 10000.0f);
    thermostat.publish_state(mode, current, target);
  }
}
