#pragma once
// Tiny non-volatile key/value store for entity state persistence (restore_mode).
// One byte per 32-bit key (typically an entity's FNV-1 key). Header-only so it
// works in device, host-sim, and host-unit-test builds without extra .cpp.
//
//   ESP32   -> Preferences (NVS), namespace "esphapi"
//   ESP8266 -> EEPROM, a small linear record table
//   host    -> in-memory std::map (per-process)
#include <cstdint>
#include <cstdio>

#if defined(ESP32)
#include <Preferences.h>
#elif defined(ESP8266)
#include <EEPROM.h>
#else
#include <map>
#endif

namespace esphome_api {
namespace nvs {

// Sentinel meaning "absent". Stored values are 0/1, so 0xFF is safe.
static constexpr uint8_t NVS_ABSENT = 0xFF;

#if defined(ESP32)

inline bool load_u8(uint32_t key, uint8_t &out) {
  Preferences p;
  if (!p.begin("esphapi", /*readOnly=*/true)) return false;
  char k[9];
  std::snprintf(k, sizeof(k), "%08x", key);
  uint8_t v = p.getUChar(k, NVS_ABSENT);
  p.end();
  if (v == NVS_ABSENT) return false;
  out = v;
  return true;
}

inline void save_u8(uint32_t key, uint8_t value) {
  Preferences p;
  if (!p.begin("esphapi", /*readOnly=*/false)) return;
  char k[9];
  std::snprintf(k, sizeof(k), "%08x", key);
  p.putUChar(k, value);
  p.end();
}

#elif defined(ESP8266)

// Record layout: [uint32 key LE][uint8 value]. Empty slots have key 0xFFFFFFFF.
static constexpr int NVS_RECORDS = 32;
static constexpr int NVS_REC_SIZE = 5;
static constexpr int NVS_BYTES = NVS_RECORDS * NVS_REC_SIZE;

inline void nvs_begin() {
  static bool inited = false;
  if (!inited) {
    EEPROM.begin(NVS_BYTES);
    inited = true;
  }
}

inline int nvs_find(uint32_t key) {
  for (int i = 0; i < NVS_RECORDS; i++) {
    int off = i * NVS_REC_SIZE;
    uint32_t k = (uint32_t)EEPROM.read(off) |
                 ((uint32_t)EEPROM.read(off + 1) << 8) |
                 ((uint32_t)EEPROM.read(off + 2) << 16) |
                 ((uint32_t)EEPROM.read(off + 3) << 24);
    if (k == key) return i;
  }
  return -1;
}

inline bool load_u8(uint32_t key, uint8_t &out) {
  nvs_begin();
  int i = nvs_find(key);
  if (i < 0) return false;
  out = EEPROM.read(i * NVS_REC_SIZE + 4);
  return true;
}

inline void save_u8(uint32_t key, uint8_t value) {
  nvs_begin();
  int i = nvs_find(key);
  if (i < 0) i = nvs_find(0xFFFFFFFF);  // first empty slot
  if (i < 0) return;                    // table full
  int off = i * NVS_REC_SIZE;
  EEPROM.write(off + 0, key & 0xFF);
  EEPROM.write(off + 1, (key >> 8) & 0xFF);
  EEPROM.write(off + 2, (key >> 16) & 0xFF);
  EEPROM.write(off + 3, (key >> 24) & 0xFF);
  EEPROM.write(off + 4, value);
  EEPROM.commit();
}

#else  // host

inline std::map<uint32_t, uint8_t> &nvs_store() {
  static std::map<uint32_t, uint8_t> s;
  return s;
}

inline bool load_u8(uint32_t key, uint8_t &out) {
  auto &s = nvs_store();
  auto it = s.find(key);
  if (it == s.end()) return false;
  out = it->second;
  return true;
}

inline void save_u8(uint32_t key, uint8_t value) { nvs_store()[key] = value; }

#endif

}  // namespace nvs
}  // namespace esphome_api
