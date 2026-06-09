// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// Wall-clock helpers for GetTime / homeassistant time sync.
#include <cstdint>

#if defined(ESP32) || defined(ESP8266)
#include <sys/time.h>
#include <time.h>
#else
#include <ctime>
#endif

namespace esphome_api {

inline uint32_t epoch_seconds() {
  return static_cast<uint32_t>(time(nullptr));
}

inline void set_epoch_seconds(uint32_t epoch) {
  if (epoch == 0) return;
#if defined(ESP32) || defined(ESP8266)
  struct timeval tv = {};
  tv.tv_sec = static_cast<time_t>(epoch);
  settimeofday(&tv, nullptr);
#else
  (void)epoch;  // host: read-only wall clock
#endif
}

}  // namespace esphome_api
