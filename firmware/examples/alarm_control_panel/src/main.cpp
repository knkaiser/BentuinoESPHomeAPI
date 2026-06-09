// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// AlarmControlPanel example: arm/disarm commands.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-alarm";
  cfg.friendly_name = "Alarm Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
AlarmControlPanel alarm;
static uint32_t state = 0;  // DISARMED

void setup() {
  Serial.begin(115200);
  delay(100);

  alarm.set_object_id("alarm");
  alarm.set_name("Alarm");
  alarm.on_command = [](uint32_t command, const std::string &code) {
    (void)code;
    state = command;
    alarm.publish_state(state);
    Serial.printf("alarm command=%u\n", command);
  };

  api.add_alarm_control_panel(&alarm);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  alarm.publish_state(state);
  Serial.println("AlarmControlPanel example ready");
}

void loop() { api.loop(); }
