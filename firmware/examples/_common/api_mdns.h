// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Home Assistant mDNS / zeroconf advertisement for the native API.
//
// The ESPHomeAPI library does not call ESPmDNS itself (WiFi must be up first,
// and the node name lives in application setup). Call advertise_api_mdns()
// once after connect_wifi() and api.begin() so cfg.mac_address is populated.
//
// Registers _esphomelib._tcp on cfg.port with TXT records Home Assistant expects.
// When Noise encryption is enabled, adds api_encryption so HA prompts for the
// PSK during setup (see COMPLIANCE_TEST_PLAN.md TS-15.4 / TS-15.5).
#pragma once

#include <Arduino.h>

#if defined(ESP32)
#include <ESPmDNS.h>
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#endif

#include "api_server.h"

namespace esphome_api {

inline bool advertise_api_mdns(APIServer &server) {
#if defined(ESP32) || defined(ESP8266)
  const DeviceConfig &cfg = server.config();
  if (!MDNS.begin(cfg.name.c_str())) {
    Serial.println("[mdns] init failed");
    return false;
  }
  MDNS.addService("esphomelib", "tcp", cfg.port);
  MDNS.addServiceTxt("esphomelib", "tcp", "version", cfg.esphome_version.c_str());
  MDNS.addServiceTxt("esphomelib", "tcp", "address",
                     WiFi.localIP().toString().c_str());
  MDNS.addServiceTxt("esphomelib", "tcp", "mac", cfg.mac_address.c_str());
  if (!cfg.friendly_name.empty()) {
    MDNS.addServiceTxt("esphomelib", "tcp", "friendly_name",
                       cfg.friendly_name.c_str());
  }
  if (server.encryption_enabled()) {
    MDNS.addServiceTxt("esphomelib", "tcp", "api_encryption",
                       "Noise_NNpsk0_25519_ChaChaPoly_SHA256");
  }
  Serial.printf("[mdns] %s.local  _esphomelib._tcp:%u%s\n", cfg.name.c_str(),
                cfg.port, server.encryption_enabled() ? " encrypted" : "");
  return true;
#else
  (void)server;
  return false;
#endif
}

}  // namespace esphome_api
