// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// Cryptographic random bytes, per platform.
#include <cstddef>
#include <cstdint>

#if defined(ESP32)
#include <esp_system.h>
#elif defined(ESP8266)
extern "C" {
#include <osapi.h>
}
#else
#include <cstdio>
#endif

namespace esphome_api {
namespace crypto {

inline void random_bytes(uint8_t *buf, size_t len) {
#if defined(ESP32)
  esp_fill_random(buf, len);
#elif defined(ESP8266)
  // Hardware RNG register on the ESP8266.
  size_t i = 0;
  while (i < len) {
    uint32_t r = *(volatile uint32_t *)0x3FF20E44;  // RANDOM_REG32
    size_t take = (len - i) < 4 ? (len - i) : 4;
    for (size_t j = 0; j < take; j++) buf[i + j] = (uint8_t)(r >> (8 * j));
    i += take;
  }
#else
  // Host build (simulator / tests): /dev/urandom.
  FILE *f = std::fopen("/dev/urandom", "rb");
  if (f) {
    size_t got = std::fread(buf, 1, len, f);
    std::fclose(f);
    if (got == len) return;
  }
  for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(i * 2654435761u);
#endif
}

}  // namespace crypto
}  // namespace esphome_api
