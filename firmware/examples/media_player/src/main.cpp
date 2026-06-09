// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// MediaPlayer example: play/pause/stop + volume.
#include <Arduino.h>
#include "api_log.h"
#include "api_server.h"
#include "wifi_setup.h"
#include "api_mdns.h"

using namespace esphome_api;

static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "example-media-player";
  cfg.friendly_name = "Media Player Example";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());
MediaPlayer speaker;
static uint32_t state = 0;  // IDLE
static float volume = 0.5f;

void setup() {
  Serial.begin(115200);
  delay(100);

  speaker.set_object_id("speaker");
  speaker.set_name("Speaker");
  speaker.set_supports_pause(true);
  speaker.on_command = [](const MediaPlayerCommand &cmd) {
    if (cmd.has_command) {
      if (cmd.command == 0) state = 1;
      else if (cmd.command == 1) state = 2;
      else if (cmd.command == 2) state = 0;
    }
    if (cmd.has_volume) volume = cmd.volume;
    speaker.publish_state(state, volume);
    Serial.printf("media state=%u volume=%.2f\n", state, volume);
  };

  api.add_media_player(&speaker);
  connect_wifi();
  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_INFO);
  speaker.publish_state(state, volume);
  Serial.println("MediaPlayer example ready");
}

void loop() { api.loop(); }
