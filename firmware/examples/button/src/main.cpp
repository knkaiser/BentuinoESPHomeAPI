// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Button example: fire-and-forget press action.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-button";
  cfg.friendly_name = "Button Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Button action;

void setup() {
  Serial.begin(115200);
  delay(100);

  action.set_object_id("action");
  action.set_name("Action");
  action.on_press = []() { Serial.println("button pressed"); };

  api.add_button(&action);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  Serial.println("Button example ready");
}

void loop() { api.loop(); }
