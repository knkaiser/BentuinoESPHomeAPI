// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Cover example: position + stop commands.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-cover";
  cfg.friendly_name = "Cover Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Cover blinds;
static float position = 0.0f;

void setup() {
  Serial.begin(115200);
  delay(100);

  blinds.set_object_id("blinds");
  blinds.set_name("Blinds");
  blinds.set_supports_position(true);
  blinds.set_supports_stop(true);
  blinds.on_command = [](const CoverCommand &cmd) {
    if (cmd.stop) {
      Serial.println("cover stop");
      return;
    }
    if (cmd.has_position) position = cmd.position;
    blinds.publish_state(position);
    Serial.printf("cover position=%.2f\n", position);
  };

  api.add_cover(&blinds);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  blinds.publish_state(position);
  Serial.println("Cover example ready");
}

void loop() { api.loop(); }
