// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Host-side unit test for the portable protocol core (no Arduino required).
// Validates wire-format output against the same vectors used by the Python
// test bed (../../testbed). Compile & run:
//   c++ -std=c++17 -I ../lib/ESPHomeAPI/src host_test.cpp -o host_test && ./host_test
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "entity.h"
#include "fnv1.h"
#include "protobuf.h"

using namespace esphome_api;

// Host stub: on-device this lives in entity.cpp and notifies APIServer.
namespace esphome_api {
void Entity::notify_changed() {}
}  // namespace esphome_api

static int failures = 0;

static std::string hex(const std::vector<uint8_t> &v) {
  static const char *d = "0123456789abcdef";
  std::string s;
  for (uint8_t b : v) {
    s.push_back(d[b >> 4]);
    s.push_back(d[b & 0xF]);
  }
  return s;
}

static void check(const char *name, bool cond, const std::string &got = "") {
  if (cond) {
    printf("  PASS  %s%s%s\n", name, got.empty() ? "" : "  -> ", got.c_str());
  } else {
    printf("  FAIL  %s%s%s\n", name, got.empty() ? "" : "  -> ", got.c_str());
    failures++;
  }
}

int main() {
  printf("ESPHomeAPI host tests\n");

  // FNV-1 vector (matches testbed TS-6.4-OFFLINE).
  check("fnv1(kmeter_temperature)==0xDB0A35EC",
        fnv1_hash("kmeter_temperature") == 0xDB0A35EC);
  check("fnv1(restart_kmeter)==0x67524C0B",
        fnv1_hash("restart_kmeter") == 0x67524C0B);

  // HelloRequest-style encoding (string field 1, uint32 fields 2,3).
  {
    ProtoWriteBuffer b;
    b.encode_string(1, "Bentuino");
    b.encode_uint32(2, 1);
    b.encode_uint32(3, 10);
    check("Hello encoding == 0a0842656e7475696e6f1001180a",
          hex(b.data) == "0a0842656e7475696e6f1001180a", hex(b.data));
  }

  // fixed32 little-endian for entity key (testbed TS-9.2).
  {
    ProtoWriteBuffer b;
    b.encode_fixed32(1, 0xDB0A35EC, true);
    check("fixed32 key == 0dec350adb", hex(b.data) == "0dec350adb", hex(b.data));
  }

  // float 23.5 (testbed TS-9.2b).
  {
    ProtoWriteBuffer b;
    b.encode_float(2, 23.5f);
    check("float 23.5 == 150000bc41", hex(b.data) == "150000bc41", hex(b.data));
  }

  // proto3 default omission.
  {
    ProtoWriteBuffer b;
    b.encode_uint32(2, 0);
    b.encode_bool(3, false);
    b.encode_string(1, "");
    check("proto3 defaults omitted", b.data.empty());
  }

  // sint32 ZigZag.
  {
    ProtoWriteBuffer b;
    b.encode_sint32(1, -1, true);
    // zigzag(-1)=1 -> tag 0x08, value 0x01
    check("sint32(-1) == 0801", hex(b.data) == "0801", hex(b.data));
  }

  // Round-trip: encode a SensorStateResponse, decode it back.
  {
    Sensor s;
    s.set_object_id("kmeter_temperature");
    s.set_name("KMeter Temperature");
    s.publish_state(23.5f);
    ProtoWriteBuffer b;
    s.encode_state(b);

    ProtoDecoder dec(b.data.data(), b.data.size());
    ProtoField f;
    uint32_t key = 0;
    float val = 0;
    bool missing = false;  // proto3 default when field 3 is omitted
    while (dec.next(f)) {
      if (f.field == 1 && f.wire == WIRE_FIXED32) key = f.as_fixed32;
      if (f.field == 2 && f.wire == WIRE_FIXED32) val = f.as_float();
      if (f.field == 3 && f.wire == WIRE_VARINT) missing = f.as_bool();
    }
    check("state key round-trips", key == 0xDB0A35EC);
    check("state value round-trips", std::fabs(val - 23.5f) < 1e-6);
    check("state not missing (field 3 omitted)", missing == false);
  }

  // Decode a SwitchCommandRequest produced by the test bed format.
  {
    // key(1, fixed32)=0x67524C0B + state(2,bool)=true
    ProtoWriteBuffer b;
    b.encode_fixed32(1, 0x67524C0B, true);
    b.encode_bool(2, true);
    ProtoDecoder dec(b.data.data(), b.data.size());
    ProtoField f;
    uint32_t key = 0;
    bool st = false;
    while (dec.next(f)) {
      if (f.field == 1) key = f.as_fixed32;
      if (f.field == 2) st = f.as_bool();
    }
    check("switch cmd decode key", key == 0x67524C0B);
    check("switch cmd decode state", st == true);
  }

  // Switch restore_mode policy (persistence + boot-state selection).
  {
    using RM = Switch::RestoreMode;

    // No saved value, default ON -> boots ON.
    {
      Switch sw;
      sw.set_object_id("restore_a");
      sw.set_restore_mode(RM::RESTORE_DEFAULT_ON);
      sw.setup();
      check("restore default-on (no saved) -> ON", sw.state() == true);
    }

    // Saved ON wins over default OFF, and drives on_control.
    {
      Switch sw;
      sw.set_object_id("restore_b");
      nvs::save_u8(sw.key(), 1);
      bool hw = false;
      sw.on_control = [&hw](bool on) { hw = on; };
      sw.set_restore_mode(RM::RESTORE_DEFAULT_OFF);
      sw.setup();
      check("restore saved ON over default OFF", sw.state() == true);
      check("restore drives on_control", hw == true);
    }

    // Saved OFF wins over default ON.
    {
      Switch sw;
      sw.set_object_id("restore_c");
      nvs::save_u8(sw.key(), 0);
      sw.set_restore_mode(RM::RESTORE_DEFAULT_ON);
      sw.setup();
      check("restore saved OFF over default ON", sw.state() == false);
    }

    // ALWAYS_OFF ignores a saved ON.
    {
      Switch sw;
      sw.set_object_id("restore_d");
      nvs::save_u8(sw.key(), 1);
      sw.set_restore_mode(RM::ALWAYS_OFF);
      sw.setup();
      check("always-off ignores saved", sw.state() == false);
    }

    // Inverted: saved ON -> boots OFF and re-persists the inverted value.
    {
      Switch sw;
      sw.set_object_id("restore_e");
      nvs::save_u8(sw.key(), 1);
      sw.set_restore_mode(RM::RESTORE_INVERTED_DEFAULT_OFF);
      sw.setup();
      uint8_t persisted = 0xFF;
      bool have = nvs::load_u8(sw.key(), persisted);
      check("inverted restore boots OFF", sw.state() == false);
      check("inverted re-persists new state", have && persisted == 0);
    }

    // NO_RESTORE never persists and boots OFF.
    {
      Switch sw;
      sw.set_object_id("restore_f");
      sw.set_restore_mode(RM::NO_RESTORE);
      sw.publish_state(true);  // would persist only if a RESTORE_* mode
      uint8_t v = 0;
      check("no-restore does not persist", !nvs::load_u8(sw.key(), v));
      Switch sw2;
      sw2.set_object_id("restore_f");
      sw2.setup();
      check("no-restore boots OFF", sw2.state() == false);
    }
  }

  // Streaming varint (frame length parsing) for payload > 127.
  {
    std::vector<uint8_t> buf;
    append_varint(buf, 300);
    size_t pos = 0;
    uint32_t out = 0;
    bool ok = stream_varint(buf.data(), buf.size(), pos, out);
    check("stream_varint(300)", ok && out == 300 && pos == 2);
  }

  printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES",
         failures, failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
