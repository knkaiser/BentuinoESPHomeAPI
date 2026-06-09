// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Example ESPHome native API device, built on the standalone ESPHomeAPI library.
//
// Exposes:
//   - sensor       "Uptime"            (seconds since boot)
//   - sensor       "Simulated Temp"    (a sine wave, demo)
//   - binary_sensor"Boot Flag"         (true after 5s)
//   - switch       "Onboard LED"       (controls GPIO LED_BUILTIN)
//   - button       "Restart"           (reboots the device)
//
// Pair it with Home Assistant (discovered via mDNS as _esphomelib._tcp, or add
// by IP) or run the Python test bed against it:
//     python run_tests.py --host <device-ip>
#include <Arduino.h>
#include <math.h>

#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include "api_log.h"
#include "api_server.h"
#include "api_mdns.h"

using namespace esphome_api;

// ----------------------------------------------------------------------------
// User configuration
// ----------------------------------------------------------------------------
#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your-password"
#endif

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ----------------------------------------------------------------------------
// Device + entities
// ----------------------------------------------------------------------------
static DeviceConfig make_config() {
  DeviceConfig cfg;
  cfg.name = "esp-api-demo";
  cfg.friendly_name = "ESP API Demo";
  cfg.model = "esp32-devkit";
  cfg.manufacturer = "Bentuino";
  cfg.password = "";  // set non-empty to require an API password
  // To enable Noise encryption, set a base64 32-byte key (the same value you
  // put under api.encryption.key in ESPHome). HA will then require the key.
  //   cfg.encryption_key = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
  cfg.port = 6053;
  return cfg;
}

APIServer api(make_config());

Sensor uptime_sensor;
Sensor temp_sensor;
BinarySensor boot_flag;
Switch led_switch;
Button restart_button;

static uint32_t last_publish_ms = 0;

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Configure entities. object_id determines the FNV-1 key HA uses.
  uptime_sensor.set_object_id("uptime");
  uptime_sensor.set_name("Uptime");
  uptime_sensor.set_unit_of_measurement("s");
  uptime_sensor.set_accuracy_decimals(0);
  uptime_sensor.set_device_class("duration");

  temp_sensor.set_object_id("simulated_temp");
  temp_sensor.set_name("Simulated Temp");
  temp_sensor.set_unit_of_measurement("\xC2\xB0""C");
  temp_sensor.set_accuracy_decimals(1);
  temp_sensor.set_device_class("temperature");

  boot_flag.set_object_id("boot_flag");
  boot_flag.set_name("Boot Flag");

  led_switch.set_object_id("onboard_led");
  led_switch.set_name("Onboard LED");
  // Remember the LED across reboots (falls back to OFF if never set).
  led_switch.set_restore_mode(Switch::RestoreMode::RESTORE_DEFAULT_OFF);
  led_switch.on_control = [](bool on) {
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
    led_switch.publish_state(on);
    Serial.printf("[switch] LED -> %s\n", on ? "ON" : "OFF");
    // Ask Home Assistant to mirror the LED onto another light entity.
    api.call_homeassistant_service(
        on ? "light.turn_on" : "light.turn_off",
        {{"entity_id", "light.desk_lamp"}});
  };

  restart_button.set_object_id("restart");
  restart_button.set_name("Restart");
  restart_button.set_device_class("restart");
  restart_button.on_press = []() {
    Serial.println("[button] restart requested");
    delay(100);
    ESP.restart();
  };

  api.add_sensor(&uptime_sensor);
  api.add_sensor(&temp_sensor);
  api.add_binary_sensor(&boot_flag);
  api.add_switch(&led_switch);
  api.add_button(&restart_button);

  // Connect WiFi.
  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());

  // Track a Home Assistant entity's state on the device.
  api.subscribe_homeassistant_state(
      "sun.sun", [](const std::string &state) {
        Serial.printf("[ha] sun.sun is now %s\n", state.c_str());
      });

  api.begin();
  advertise_api_mdns(api);
  api.enable_serial_log_bridge(LOG_LEVEL_VERBOSE);
  Serial.println("ESPHome native API server started on port 6053");
}

void loop() {
  api.loop();

  uint32_t now = millis();
  if (now - last_publish_ms >= 2000) {
    last_publish_ms = now;

    uptime_sensor.publish_state(now / 1000.0f);

    float t = 22.0f + 3.0f * sinf(now / 10000.0f);
    temp_sensor.publish_state(t);

    boot_flag.publish_state(now > 5000);

    Serial.printf("publish tick uptime=%.0fs temp=%.1f\n", now / 1000.0f, t);
  }
}
