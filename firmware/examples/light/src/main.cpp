// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Light example: on/off + brightness (PWM on LIGHT_PIN).
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

#ifndef LIGHT_PIN
#define LIGHT_PIN 2
#endif
static constexpr uint32_t COLOR_MODE_BRIGHTNESS = 3;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-light";
  cfg.friendly_name = "Light Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Light lamp;
static bool on = false;
static float brightness = 0.5f;

static void apply() {
  int duty = on ? (int)(brightness * 255.0f + 0.5f) : 0;
  analogWrite(LIGHT_PIN, duty);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(LIGHT_PIN, OUTPUT);
  apply();

  lamp.set_object_id("lamp");
  lamp.set_name("Lamp");
  lamp.set_supported_color_modes({COLOR_MODE_BRIGHTNESS});
  lamp.on_command = [](const LightCommand &cmd) {
    if (cmd.has_state) on = cmd.state;
    if (cmd.has_brightness) brightness = cmd.brightness;
    apply();
    lamp.publish_state(on, brightness, COLOR_MODE_BRIGHTNESS);
    Serial.printf("light on=%d brightness=%.2f\n", on, brightness);
  };

  api.add_light(&lamp);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  lamp.publish_state(on, brightness, COLOR_MODE_BRIGHTNESS);
  Serial.println("Light example ready");
}

void loop() { api.loop(); }
