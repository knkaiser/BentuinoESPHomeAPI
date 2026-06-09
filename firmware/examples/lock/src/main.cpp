// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Lock example: lock / unlock commands.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-lock";
  cfg.friendly_name = "Lock Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
Lock door;
static uint32_t state = 1;  // LOCKED

void setup() {
  Serial.begin(115200);
  delay(100);

  door.set_object_id("door");
  door.set_name("Door Lock");
  door.on_command = [](uint32_t command) {
    if (command == 0) state = 0;       // UNLOCK
    else if (command == 1) state = 1;  // LOCK
    door.publish_state(state);
    Serial.printf("lock command=%u\n", command);
  };

  api.add_lock(&door);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  door.publish_state(state);
  Serial.println("Lock example ready");
}

void loop() { api.loop(); }
