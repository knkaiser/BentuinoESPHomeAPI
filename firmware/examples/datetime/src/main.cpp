// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// DateTime example: Unix epoch timestamp picker.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-datetime";
  cfg.friendly_name = "DateTime Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
DateTime appointment;

void setup() {
  Serial.begin(115200);
  delay(100);

  appointment.set_object_id("appointment");
  appointment.set_name("Appointment");
  appointment.on_control = [](uint32_t epoch) {
    Serial.printf("datetime epoch=%u\n", epoch);
  };

  api.add_datetime(&appointment);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  appointment.publish_state(1717920000);
  Serial.println("DateTime example ready");
}

void loop() { api.loop(); }
