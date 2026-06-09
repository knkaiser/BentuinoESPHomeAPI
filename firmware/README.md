# ESPHome Native API — C++ Device Implementation (PlatformIO)

A from-scratch implementation of the **device/server side** of the ESPHome
native API, written in portable C++ for ESP32 / ESP32-C3 / ESP8266 using the
Arduino framework. It lets a custom firmware be discovered and controlled by
Home Assistant exactly like a real ESPHome node — without depending on the
ESPHome codebase.

It implements the protocol described in [`../README.md`](../README.md) and
[`../IMPLEMENTATION_GUIDE.md`](../IMPLEMENTATION_GUIDE.md), and is validated
against the same wire vectors as the [Python test bed](../testbed/README.md).

**See [`REFERENCE.md`](REFERENCE.md) for the full library API reference and the
list of open items / limitations.**

## Status

| Feature | Status |
|---|---|
| TCP transport on port 6053 (`TCP_NODELAY`) | ✅ |
| Plaintext framing (`0x00` indicator) | ✅ |
| Connection lifecycle (Hello / Connect) | ✅ |
| Application password authentication | ✅ |
| Access control (privileged msgs require auth) | ✅ |
| Keepalive (Ping/Pong, idle timeout) | ✅ |
| `DeviceInfo` | ✅ |
| Entity discovery (`ListEntities*` + Done) | ✅ |
| State subscription + push (`*StateResponse`) | ✅ |
| Commands (`SwitchCommandRequest`, `ButtonCommandRequest`) | ✅ |
| All standard entity types (sensor, binary_sensor, text_sensor, switch, button, cover, fan, light, number, select, lock, climate, media_player, alarm_control_panel, text, date, time, datetime, valve, update, event, camera) | ✅ |
| FNV-1 entity keys | ✅ |
| Self-contained protobuf codec (no nanopb/protoc) | ✅ |
| Noise encryption (`Noise_NNpsk0_25519_ChaChaPoly_SHA256`) | ✅ |
| Plaintext transport (`0x00`) | ✅ |
| Log streaming (`SubscribeLogs` 28/29) | ✅ |
| Serial → `api.log()` bridge | ✅ |
| Time sync (`GetTime` 36/37) | ✅ |
| HA service calls / events (34/35) | ✅ |
| HA state subscription (38/39/40) | ✅ |
| Switch state persistence (`restore_mode`) | ✅ |

Transport is selected automatically: if `encryption_key` is set the device
**requires** Noise (`0x01`) and refuses plaintext; otherwise it accepts
plaintext (`0x00`). The crypto is self-contained (vendored TweetNaCl for
X25519 + in-tree SHA-256/HKDF and ChaCha20-Poly1305) — no external library or
`protoc`/nanopb needed — so it builds identically on ESP32, ESP8266, and host.

## Build size (reference, with Noise)

| Target | RAM | Flash |
|---|---|---|
| esp32dev | 11.3% | 46.1% |
| esp8266 (nodemcuv2) | 38.4% | 29.3% |

## Layout

```
firmware/
  platformio.ini            # esp32dev / esp32-c3 / nodemcuv2 envs
  src/main.cpp              # demo app: sensors, a switch, a button
  examples/                 # one minimal example per entity type (22 total)
    _common/                  # wifi_setup.h, api_mdns.h (HA discovery)
    sensor/ light/ switch/ …  # see examples/README.md
  test/host_test.cpp       # host unit test for the portable core
  lib/ESPHomeAPI/
    library.json
    src/
      fnv1.h               # FNV-1 entity-key hash
      protobuf.h           # portable protobuf writer/reader + varint helpers
      api_message_types.h  # message-type IDs + version constants
      api_log.h            # LogLevel enum
      platform_time.h      # epoch_seconds() get/set (GetTime sync)
      platform_nvs.h       # portable key/value persistence (restore_mode)
      entity.h             # Entity base + setup() hook
      entity_types.h       # all 22 entity subclasses
      entity.cpp           # publish_state -> server notify
      serial_log_bridge.h/.cpp     # Serial -> api.log() bridge (ApiSerial)
      serial_log_bridge_install.h  # opt-in macro: #define Serial ApiSerial
      api_connection.h/.cpp# framing (plaintext + Noise) + lifecycle
      api_server.h/.cpp    # TCP server, entity registry, state push
      crypto/
        noise.h            # Noise_NNpsk0 responder
        sha256.h           # SHA-256 + HMAC + Noise HKDF
        chachapoly.h       # ChaCha20-Poly1305 AEAD (RFC 8439)
        random.h           # platform RNG (esp32/esp8266/host)
        tweetnacl.c/.h     # vendored X25519 (public domain)
        tweetnacl_glue.cpp # randombytes() provider
```

The files `fnv1.h`, `protobuf.h`, `api_message_types.h`, and `entity.h` are
Arduino-independent and can be unit-tested on a host.

## Quick start

1. Set your WiFi credentials — either edit the defaults in `src/main.cpp` or
   pass them as build flags in `platformio.ini`:

```ini
build_flags =
    -std=gnu++17
    -DWIFI_SSID='"my-ssid"'
    -DWIFI_PASSWORD='"my-pass"'
```

2. Build and upload:

```bash
pio run -e esp32dev -t upload
pio device monitor
```

3. In Home Assistant: **Settings → Devices & Services → Add Integration →
   ESPHome**, enter the device IP, port `6053`. If you set `encryption_key`,
   enter that base64 key when prompted; otherwise leave it empty (plaintext).

## Examples

Standalone, ready-to-flash projects live in [`examples/`](examples/). Each is a
self-contained PlatformIO project that pulls in the library from `lib/`:

- **[`examples/`](examples/)** — one minimal, self-contained project per entity
  type (22 total: `sensor`, `light`, `switch`, `fan`, `climate`, …).

```bash
cd examples/light   # or sensor, switch, fan, …
pio run -e esp32dev -t upload && pio device monitor
```

## Validate with the Python test bed

The companion test bed can run the compliance suite against this firmware.
Plaintext (no key configured):

```bash
cd ../testbed
python run_tests.py --host <device-ip> --out fw_results
```

Encrypted (key configured on the device) — this also runs the Noise tests:

```bash
python run_tests.py --host <device-ip> \
    --encryption-key "AAECAwQ...=" --out fw_noise_results
```

You can also run the full suite **without hardware**: the real server code in
`lib/ESPHomeAPI` is compiled into a host TCP simulator (`test/host_sim/`, using
a POSIX shim for `WiFiServer`/`WiFiClient`) and tested over `localhost`,
including the Noise handshake and encrypted transport. See
[`../TEST_STRATEGY.md`](../TEST_STRATEGY.md) for the full testing architecture
(latest result over Noise: 38/38 MUST passing; 34/34 in plaintext mode).

## Using the library in your own project

Register entities, then drive the server from `loop()`:

```cpp
#include "api_server.h"
using namespace esphome_api;

DeviceConfig cfg;          // set name, model, password, port...
APIServer api(cfg);

Sensor temperature;
Switch relay;

void setup() {
  temperature.set_object_id("temperature");   // -> FNV-1 key
  temperature.set_name("Temperature");
  temperature.set_unit_of_measurement("°C");

  relay.set_object_id("relay");
  relay.set_name("Relay");
  relay.on_control = [](bool on) { digitalWrite(RELAY_PIN, on); };

  api.add_sensor(&temperature);
  api.add_switch(&relay);

  // ... connect WiFi ...
  api.begin();
}

void loop() {
  api.loop();
  temperature.publish_state(read_temp());   // pushed to subscribed HA clients
}
```

## Host unit test

```bash
cd test
c++ -std=c++17 -I ../lib/ESPHomeAPI/src host_test.cpp -o host_test && ./host_test
```

Verifies FNV-1 vectors, protobuf tag/fixed32/float/zigzag encoding, proto3
default omission, request decoding against the test bed's expected bytes, and
the `Switch` `restore_mode` persistence policies.

## Adding entity types

Subclass `Entity` (see `entity.h`) and implement `list_type()`, `encode_list()`,
`state_type()`, `encode_state()`, and `kind()`. Field numbers come from
`../api.proto`.

## Notes & limitations

- Supports both Noise encryption (`0x01`) and plaintext (`0x00`); the mode is
  chosen automatically from whether `encryption_key` is set. Even so, do not
  expose the device directly to the internet.
- One device, multiple simultaneous HA connections are supported.
- State persistence across reboots is currently implemented for `Switch`
  (`restore_mode`); other stateful entities (light, fan, …) are not yet
  persisted.
- Heap-allocated `std::vector`/`std::string` are used for clarity; fine on ESP32,
  and comfortably within ESP8266 limits for a handful of entities.
