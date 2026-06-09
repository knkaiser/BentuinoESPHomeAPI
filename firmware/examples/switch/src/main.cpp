// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Switch example: on/off output with optional restore_mode.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

#ifndef OUTPUT_PIN
#define OUTPUT_PIN 5
#endif

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-switch";
  cfg.friendly_name = "Switch Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Switch relay;

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);

  relay.set_object_id("relay");
  relay.set_name("Relay");
  relay.set_restore_mode(Switch::RestoreMode::RESTORE_DEFAULT_OFF);
  relay.on_control = [](bool on) {
    digitalWrite(OUTPUT_PIN, on ? HIGH : LOW);
    Serial.printf("relay -> %s\n", on ? "ON" : "OFF");
  };

  api.add_switch(&relay);
  connect_wifi();
  api.begin();  // restores relay from NVS
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  Serial.println("Switch example ready");
}

void loop() { api.loop(); }
