// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// ESPHome entity key = FNV-1 32-bit hash of the object_id.
// (Multiply-then-XOR variant; NOT FNV-1a.)
#include <cstdint>
#include <string>

namespace esphome_api {

inline uint32_t fnv1_hash(const std::string &object_id) {
  uint32_t hash = 2166136261UL;
  for (unsigned char c : object_id) {
    hash *= 16777619UL;
    hash ^= c;
  }
  return hash;
}

}  // namespace esphome_api
