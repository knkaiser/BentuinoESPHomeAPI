// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Time example: hour / minute / second picker.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-time";
  cfg.friendly_name = "Time Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
TimeEntity schedule;

void setup() {
  Serial.begin(115200);
  delay(100);

  schedule.set_object_id("schedule");
  schedule.set_name("Schedule Time");
  schedule.on_control = [](uint32_t h, uint32_t m, uint32_t s) {
    Serial.printf("time -> %02u:%02u:%02u\n", h, m, s);
  };

  api.add_time(&schedule);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  schedule.publish_state(8, 30, 0);
  Serial.println("Time example ready");
}

void loop() { api.loop(); }
