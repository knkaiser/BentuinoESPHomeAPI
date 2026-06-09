// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Host simulator: runs the REAL ESPHomeAPI server code (api_server.cpp,
// api_connection.cpp, entity.cpp) over a POSIX TCP socket on localhost:6053 so
// the Python test bed can run the compliance suite against it.
//
// Build (from firmware/):
//   cc  -O2 -c lib/ESPHomeAPI/src/crypto/tweetnacl.c -o /tmp/tweetnacl.o
//   c++ -std=c++17 -I lib/ESPHomeAPI/src -I test/host_sim \
//       test/host_sim/main.cpp lib/ESPHomeAPI/src/api_connection.cpp \
//       lib/ESPHomeAPI/src/api_server.cpp lib/ESPHomeAPI/src/entity.cpp \
//       lib/ESPHomeAPI/src/serial_log_bridge.cpp \
//       lib/ESPHomeAPI/src/crypto/tweetnacl_glue.cpp /tmp/tweetnacl.o \
//       -o test/host_sim/sim
//
// Run plaintext:  ./test/host_sim/sim
// Run with Noise: SIM_ENCRYPTION_KEY="<base64-32-byte-psk>" ./test/host_sim/sim
#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

#include "api_log.h"
#include "api_server.h"
#include "serial_log_bridge.h"

using namespace esphome_api;

// Configuration is overridable via environment variables so the same binary can
// be run in plaintext or Noise mode:
//   SIM_PASSWORD        application password (default "testpass")
//   SIM_ENCRYPTION_KEY  base64 32-byte Noise PSK (default: none = plaintext)
static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "sim-device";
  cfg.friendly_name = "Sim Device";
  cfg.model = "host-sim";
  cfg.manufacturer = "Bentuino";
  const char *pw = getenv("SIM_PASSWORD");
  cfg.password = pw ? pw : "testpass";
  const char *key = getenv("SIM_ENCRYPTION_KEY");
  if (key) cfg.encryption_key = key;
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());

Sensor uptime_sensor;
Sensor temp_sensor;
BinarySensor boot_flag;
Switch led_switch;
Button restart_button;

int main() {
  uptime_sensor.set_object_id("uptime");
  uptime_sensor.set_name("Uptime");
  uptime_sensor.set_unit_of_measurement("s");
  uptime_sensor.set_accuracy_decimals(0);

  temp_sensor.set_object_id("simulated_temp");
  temp_sensor.set_name("Simulated Temp");
  temp_sensor.set_unit_of_measurement("C");
  temp_sensor.set_accuracy_decimals(1);

  boot_flag.set_object_id("boot_flag");
  boot_flag.set_name("Boot Flag");

  led_switch.set_object_id("onboard_led");
  led_switch.set_name("Onboard LED");
  led_switch.on_control = [](bool on) {
    std::printf("[sim] LED -> %s\n", on ? "ON" : "OFF");
  };

  restart_button.set_object_id("restart");
  restart_button.set_name("Restart");
  restart_button.set_device_class("restart");
  restart_button.on_press = []() { std::printf("[sim] restart pressed\n"); };

  api.add_sensor(&uptime_sensor);
  api.add_sensor(&temp_sensor);
  api.add_binary_sensor(&boot_flag);
  api.add_switch(&led_switch);
  api.add_button(&restart_button);

  // Seed initial states.
  uptime_sensor.publish_state(0);
  temp_sensor.publish_state(22.0f);
  boot_flag.publish_state(true);

  // Watch a Home Assistant entity. When HA pushes its state, log it so the
  // test bed can observe the round-trip via SubscribeLogs.
  api.subscribe_homeassistant_state(
      "sun.sun", [](const std::string &state) {
        ApiSerial.printf("HA state sun.sun=%s\n", state.c_str());
      });

  api.begin();
  api.enable_serial_log_bridge(LOG_LEVEL_DEBUG);
  std::printf("[sim] ESPHome native API server listening on 0.0.0.0:6053\n");
  std::fflush(stdout);

  uint32_t last = 0;
  while (true) {
    api.loop();
    uint32_t now = millis();
    if (now - last >= 2000) {
      last = now;
      uptime_sensor.publish_state(now / 1000.0f);
      temp_sensor.publish_state(22.0f + 3.0f * sinf(now / 10000.0f));
      boot_flag.publish_state(now > 5000);
      ApiSerial.printf("sim heartbeat uptime=%.0fs\n", now / 1000.0f);
      // Fire a Home Assistant event each tick (delivered only to clients that
      // subscribed to HA services).
      api.fire_homeassistant_event(
          "esphome.sim_heartbeat",
          {{"uptime", std::to_string(now / 1000)}});
    }
    usleep(2000);  // ~2ms cooperative yield
  }
  return 0;
}
