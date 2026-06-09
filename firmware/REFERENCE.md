# ESPHomeAPI Library — Reference

API reference for the standalone C++ ESPHome native API library
(`firmware/lib/ESPHomeAPI`). For setup and build instructions see
[`README.md`](README.md). For the protocol itself see
[`../README.md`](../README.md) and [`../IMPLEMENTATION_GUIDE.md`](../IMPLEMENTATION_GUIDE.md).

- Library version: `0.4.1`
- Namespace: `esphome_api`
- Language standard: C++17
- Targets: ESP32, ESP32-C3, ESP8266 (Arduino framework)
- Transport: plaintext (`0x00`) and Noise (`Noise_NNpsk0_25519_ChaChaPoly_SHA256`, `0x01`)

---

## Table of Contents

1. [Concepts](#1-concepts)
2. [Quick start](#2-quick-start)
3. [Headers](#3-headers)
4. [`DeviceConfig`](#4-deviceconfig)
5. [`APIServer`](#5-apiserver)
6. [Entities](#6-entities)
   - [`Entity` (base)](#61-entity-base)
   - [Worked examples](#62-worked-examples)
   - [All entity types](#63-all-entity-types)
   - [Event & Camera](#64-event--camera)
7. [Protobuf codec](#7-protobuf-codec)
8. [FNV-1 hash](#8-fnv-1-hash)
9. [Message type IDs](#9-message-type-ids)
10. [Runtime model & lifecycle](#10-runtime-model--lifecycle)
11. [Extending the library](#11-extending-the-library)
12. [Open Items / Limitations](#open-items--limitations)

---

## 1. Concepts

The library exposes a small object model that mirrors ESPHome:

- **`APIServer`** owns a TCP listener on port 6053, a registry of entities, and
  a set of live client connections. You drive it from `loop()`.
- **`Entity`** subclasses (see [§6](#6-entities)) describe what the device
  exposes to Home Assistant. Each entity's `object_id`
  is hashed (FNV-1) into the 32-bit `key` Home Assistant uses to address it.
- **`APIConnection`** (internal) implements per-client framing (plaintext or
  Noise), the Hello → Connect → subscribe lifecycle, and command dispatch. You
  normally don't touch it.

Data flow:

```
publish_state()  ──> APIServer::push_state()  ──> subscribed APIConnections
publish_event()  ──> APIServer::push_event()  ──> EventResponse to subscribers
send_image()     ──> APIServer::push_camera_image() ──> CameraImageResponse

HA *CommandRequest ──> APIConnection ──> find_by_key() ──> Entity::handle_command()
```

---

## 2. Quick start

```cpp
#include "api_server.h"
using namespace esphome_api;

DeviceConfig cfg;
APIServer api(cfg);

Sensor temperature;
Switch relay;

void setup() {
  temperature.set_object_id("temperature");      // -> FNV-1 key
  temperature.set_name("Temperature");
  temperature.set_unit_of_measurement("\u00B0C");

  relay.set_object_id("relay");
  relay.set_name("Relay");
  relay.on_control = [](bool on) { digitalWrite(RELAY_PIN, on); };

  api.add_sensor(&temperature);
  api.add_switch(&relay);

  // ... connect WiFi here ...
  api.begin();                                    // start TCP server
}

void loop() {
  api.loop();                                     // service the API
  temperature.publish_state(read_temp());         // push to subscribed clients
}
```

**Noise encryption** — set the same base64 32-byte key as ESPHome's
`api.encryption.key`:

```cpp
cfg.encryption_key = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
```

When set, the device **requires** Noise (`0x01`) and rejects plaintext clients.

---

## 3. Headers

| Header | Provides | Arduino-dependent |
|---|---|---|
| `api_server.h` | `APIServer`, `DeviceConfig` | yes (WiFi) |
| `api_connection.h` | `APIConnection` (internal) | yes (WiFi) |
| `entity.h` / `entity_types.h` | `Entity` base + all entity subclasses | no |
| `protobuf.h` | `ProtoWriteBuffer`, `ProtoDecoder`, `ProtoField`, varint helpers | no |
| `fnv1.h` | `fnv1_hash()` | no |
| `api_message_types.h` | `MessageType` enum, version constants | no |
| `api_log.h` | `LogLevel` enum | no |
| `platform_time.h` | `epoch_seconds()`, `set_epoch_seconds()` | platform-specific |
| `platform_nvs.h` | `load_u8()` / `save_u8()` (switch `restore_mode`) | platform-specific |
| `serial_log_bridge.h` | `SerialLogBridge`, global `ApiSerial` | yes (UART) |
| `serial_log_bridge_install.h` | opt-in `#define Serial ApiSerial` macro | yes (UART) |
| `crypto/noise.h` | `NoiseResponder` (NNpsk0) | no |
| `crypto/sha256.h` | SHA-256, HMAC, Noise HKDF | no |
| `crypto/chachapoly.h` | ChaCha20-Poly1305 AEAD | no |
| `crypto/random.h` | `random_bytes()` | platform-specific |
| `crypto/tweetnacl.{c,h}` | X25519 (vendored, public domain) | no |
| `crypto/tweetnacl_glue.cpp` | `randombytes()` for TweetNaCl | platform RNG |

Including `api_server.h` transitively pulls in `entity.h` → `entity_types.h`
and the codec. The non-Arduino headers can be compiled and unit-tested on a host
(see `test/host_test.cpp`). The full server stack is tested via `test/host_sim/`.

---

## 4. `DeviceConfig`

Plain struct describing the device. Pass it to the `APIServer` constructor.

```cpp
struct DeviceConfig {
  std::string name             = "esp-device";
  std::string friendly_name    = "ESP Device";
  std::string model            = "custom";
  std::string manufacturer     = "DIY";
  std::string esphome_version  = "2024.1.0-cpp";
  std::string compilation_time = __DATE__ " " __TIME__;
  std::string mac_address;            // auto-filled from WiFi if empty
  std::string password = "";          // empty => no application password
  std::string encryption_key = "";    // base64 32-byte Noise PSK; empty => plaintext
  bool        time_sync_from_ha = true;  // request time from HA when clock unset
  uint16_t    port     = 6053;
};
```

| Field | Meaning | Surfaced in |
|---|---|---|
| `name` | Node name (lowercase, no spaces) | `HelloResponse.name`, `DeviceInfoResponse.name` |
| `friendly_name` | Display name | `DeviceInfoResponse.friendly_name` |
| `model` | Hardware model string | `DeviceInfoResponse.model` |
| `manufacturer` | Manufacturer string | `DeviceInfoResponse.manufacturer` |
| `esphome_version` | Reported firmware version | `DeviceInfoResponse.esphome_version` |
| `compilation_time` | Build timestamp | `DeviceInfoResponse.compilation_time` |
| `mac_address` | MAC; if empty, `WiFi.macAddress()` is used in `begin()` | `DeviceInfoResponse.mac_address`, Noise server hello |
| `password` | Application password; empty disables the check | `ConnectRequest` validation |
| `encryption_key` | Base64 32-byte Noise PSK. When set, the device **requires** Noise and rejects plaintext. Decoded in `begin()`. | Noise transport |
| `time_sync_from_ha` | After successful Connect, if wall clock is unset (`epoch == 0`), send `GetTimeRequest` to HA and apply `GetTimeResponse`. | Time sync |
| `port` | TCP listen port | listener |

> The struct is copied into the server. Set fields **before** constructing the
> `APIServer` (or before `begin()`), since `begin()` reads them.

---

## 5. `APIServer`

The top-level object. Construct with a `DeviceConfig`, register entities, then
call `begin()` and `loop()`.

### Construction

```cpp
explicit APIServer(const DeviceConfig &cfg);
```

### Entity registration

```cpp
void add_entity(Entity *entity);   // generic — preferred

// Typed convenience aliases (all call add_entity):
void add_sensor(Sensor *);
void add_binary_sensor(BinarySensor *);
void add_text_sensor(TextSensor *);
void add_switch(Switch *);
void add_button(Button *);
void add_cover(Cover *);
void add_fan(Fan *);
void add_light(Light *);
void add_number(Number *);
void add_select(Select *);
void add_lock(Lock *);
void add_climate(Climate *);
void add_media_player(MediaPlayer *);
void add_alarm_control_panel(AlarmControlPanel *);
void add_text(Text *);
void add_date(Date *);
void add_time(TimeEntity *);
void add_datetime(DateTime *);
void add_valve(Valve *);
void add_update(Update *);
void add_event(Event *);
void add_camera(Camera *);
```

Registration sets the entity's owning server pointer (so `publish_state()` /
`publish_event()` / `send_image()` can notify) and appends it to the registry.
**Entities must outlive the server** (use globals/statics, as in the example).
Register all entities **before** `begin()` so the first `ListEntities` is
complete.

### Lifecycle

```cpp
void begin();   // resolve MAC, decode encryption_key if set, start TCP listener
void loop();    // accept new clients, service existing ones, prune dead ones
```

Call `begin()` once after WiFi is connected. Call `loop()` every Arduino
`loop()` iteration (non-blocking).

### State, events, camera, logs & lookup

```cpp
void push_state(Entity *entity);
void push_event(Event *entity, const std::string &event_type);
void push_camera_image(Camera *camera, const uint8_t *data, size_t len, bool done);
void dispatch_camera_image_request(bool single, bool stream);

void log(uint32_t level, const std::string &message);
void dump_config();

void enable_serial_log_bridge(uint32_t level);
void disable_serial_log_bridge();

Entity *find_by_key(uint32_t key);
```

`push_state()` is normally invoked by `Entity::publish_state()`.
`push_event()` / `push_camera_image()` are invoked by `Event::publish_event()`
and `Camera::send_image()` respectively. `dispatch_camera_image_request()` is
called internally when HA sends `CameraImageRequest` (no key in that message —
all registered `Camera` entities receive `on_image_request`).

**Log streaming:** `log()` sends `SubscribeLogsResponse` (29) to clients that
sent `SubscribeLogsRequest` with a sufficient minimum `level`. Use `LogLevel`
from `api_log.h` (`LOG_LEVEL_INFO`, `LOG_LEVEL_ERROR`, …). If the TCP send
buffer is full, `send_failed = true` is sent per the protocol. `dump_config()`
emits CONFIG-level lines (device name, port, encryption, entity list) and runs
automatically when HA subscribes with `dump_config = true`.

**Serial → log bridge:** `enable_serial_log_bridge(level)` attaches the global
`ApiSerial` object (`serial_log_bridge.h`). It line-buffers `Print` output,
forwards bytes to the hardware UART (or stdout on the host simulator), and
mirrors each complete line to `log()` at the given level. On ESP32/ESP8266 the
demo enables this automatically via `platformio.ini` `build_src_flags` (app
`src/` only — library translation units keep the real UART):

```
build_src_flags =
  -DESPHOME_API_SERIAL_BRIDGE
  -Ilib/ESPHomeAPI/src
  -include serial_log_bridge_install.h   ; #define Serial ApiSerial
```

Call `enable_serial_log_bridge()` after `begin()`. Use `Serial.println()` as
usual — HA log subscribers receive the same text. `disable_serial_log_bridge()`
stops mirroring without affecting UART output. Library `.cpp` files always use
the real `::Serial` UART and are not macro-patched.

### Home Assistant integration

```cpp
// Call a HA service (or fire an event) — delivered to clients that subscribed.
void call_homeassistant_service(const HomeAssistantServiceCall &call);
void call_homeassistant_service(const std::string &service,
                                const std::vector<HAKeyValue> &data = {});
void fire_homeassistant_event(const std::string &event,
                              const std::vector<HAKeyValue> &data = {});

// Track a HA entity (or one attribute); callback fires on each push.
void subscribe_homeassistant_state(
    const std::string &entity_id, const std::string &attribute,
    std::function<void(const std::string &state)> on_state, bool once = false);
void subscribe_homeassistant_state(
    const std::string &entity_id,
    std::function<void(const std::string &state)> on_state);  // main state
```

`HAKeyValue` is `std::pair<std::string, std::string>`. `HomeAssistantServiceCall`
carries `service`, `data`, `data_template`, `variables`, and `is_event`.

**Calling services (device → HA):** HA enables this by sending
`SubscribeHomeassistantServicesRequest` (34). After that, each
`call_homeassistant_service()` / `fire_homeassistant_event()` broadcasts a
`HomeassistantServiceResponse` (35) to every subscribed client. Calls made
before any client subscribes are dropped (not queued).

**Reading HA states (device ← HA):** register interest with
`subscribe_homeassistant_state()` **before `begin()`**. When HA sends
`SubscribeHomeAssistantStatesRequest` (38), the device replies with one
`SubscribeHomeAssistantStateResponse` (39) per registration. HA then pushes
`HomeAssistantStateResponse` (40) values, which invoke the matching callback
(matched on `entity_id` + `attribute`).

### Time sync

- **Inbound `GetTimeRequest` (36):** setup-level (after Hello, before Connect).
  Answered with `GetTimeResponse` and `epoch_seconds()` from `platform_time.h`.
- **Outbound `GetTimeRequest`:** after successful Connect, only when
  `time_sync_from_ha` is true **and** `epoch_seconds() == 0` (e.g. ESP32 before
  NTP). HA's `GetTimeResponse` calls `settimeofday()` on ESP32/ESP8266.

### Accessors

```cpp
const DeviceConfig          &config()            const;
const std::vector<Entity *> &entities()          const;
bool                         encryption_enabled() const;  // true when PSK decoded
const uint8_t               *psk()               const;   // 32 bytes when enabled
```

---

## 6. Entities

### 6.1 `Entity` (base)

Abstract base for all exposed entities.

**Configuration**

```cpp
void set_object_id(const std::string &oid);  // sets object_id AND computes key
void set_name(const std::string &name);
void set_unique_id(const std::string &uid);  // defaults to object_id
void set_icon(const std::string &icon);      // e.g. "mdi:thermometer"
void set_entity_category(uint32_t cat);      // EntityCategory enum (proto)
void set_disabled_by_default(bool d);
```

**Accessors**

```cpp
const std::string &object_id() const;
const std::string &name()      const;
uint32_t           key()       const;        // FNV-1(object_id)
```

**Virtual interface (implemented by subclasses)**

```cpp
virtual uint16_t list_type()  const;                  // ListEntities* msg ID
virtual void     encode_list(ProtoWriteBuffer &) const;
virtual uint16_t state_type() const;                  // *StateResponse ID (0 = none)
virtual void     encode_state(ProtoWriteBuffer &) const;
virtual const char *kind() const;                     // "sensor", "switch", ...

// Default returns false. Controllable entities override to decode commands.
virtual bool handle_command(uint16_t msg_type, const uint8_t *payload, size_t len);

// One-time init, called once per entity by APIServer::begin(). Default: no-op.
// Switch overrides it to restore persisted state (see restore_mode).
virtual void setup();
```

> `set_object_id()` is what makes the entity addressable. Call it for every
> entity; the resulting `key` must match `FNV-1(object_id)` (it does by
> construction). `set_name()` only affects the display label.

### 6.2 Worked examples

#### `Sensor`

Numeric (float) read-only entity.

```cpp
void set_unit_of_measurement(const std::string &u);
void set_accuracy_decimals(int32_t d);     // default 1
void set_device_class(const std::string &dc);
void set_state_class(uint32_t sc);
void set_force_update(bool f);

void  publish_state(float value);          // NaN => reported as missing_state
float state() const;
```

`publish_state()` stores the value, marks the state present (unless `NaN`), and
pushes a `SensorStateResponse` to subscribed clients.

#### `BinarySensor`

Boolean read-only entity.

```cpp
void set_device_class(const std::string &dc);

void publish_state(bool value);
bool state() const;
```

#### `TextSensor`

```cpp
void set_device_class(const std::string &dc);
void publish_state(const std::string &value);
const std::string &state() const;
```

#### `Switch`

Boolean controllable entity.

```cpp
void set_device_class(const std::string &dc);
void set_assumed_state(bool a);
void set_restore_mode(Switch::RestoreMode m);  // boot-state policy (see below)

std::function<void(bool)> on_control;      // set this to act on HA commands

void control(bool value);                  // called internally on HA command
void publish_state(bool value);            // report state without a command
bool state() const;
```

When Home Assistant sends a `SwitchCommandRequest`, the connection calls
`control(value)`, which invokes `on_control(value)` (your hardware action) and
then `publish_state(value)` to echo the new state. To reflect an externally
changed state (e.g. a physical button), call `publish_state()` directly.

Commands are handled via `Entity::handle_command()` (not a separate connection
hook per type).

**State persistence (`restore_mode`).** By default a switch is volatile and
boots OFF. Set a restore mode to persist the state to non-volatile storage and
choose how it is applied at boot:

```cpp
enum class Switch::RestoreMode : uint8_t {
  NO_RESTORE,                    // volatile; boots OFF (default)
  RESTORE_DEFAULT_OFF,           // restore saved; if none, OFF
  RESTORE_DEFAULT_ON,            // restore saved; if none, ON
  ALWAYS_OFF,                    // ignore saved, force OFF
  ALWAYS_ON,                     // ignore saved, force ON
  RESTORE_INVERTED_DEFAULT_OFF,  // restore inverse of saved; if none, OFF
  RESTORE_INVERTED_DEFAULT_ON,   // restore inverse of saved; if none, ON
};
```

Only `RESTORE_*` modes write to storage (on every `publish_state`). The value
is restored in `Entity::setup()`, which `APIServer::begin()` calls once per
entity; restoring drives `on_control` so the hardware (relay/GPIO) matches the
restored state. Storage is abstracted in `platform_nvs.h` — ESP32 uses NVS
(`Preferences`), ESP8266 uses an `EEPROM` record table, and the host build uses
an in-process map. Call `set_object_id()` and `set_restore_mode()` **before**
`begin()`; the FNV-1 key derived from `object_id` is the storage key.

#### `Button`

Stateless, command-only entity.

```cpp
void set_device_class(const std::string &dc);

std::function<void()> on_press;            // set this to act on HA presses

void press();                              // called internally on HA command
```

`state_type()` returns `0`; buttons emit no state messages and are skipped
during state subscription.

### 6.3 All entity types

Every **standard ESPHome entity platform** exposed to Home Assistant via the
native API is implemented in `entity_types.h` (22 types). Register with
`api.add_entity(&e)` or `api.add_<kind>(&e)`.

| Class | `kind()` | List ID | State ID | Cmd ID | Callback / API |
|---|---|---:|---:|---:|---|
| `BinarySensor` | `binary_sensor` | 12 | 21 | — | `publish_state(bool)` |
| `Cover` | `cover` | 13 | 22 | 30 | `on_command(CoverCommand)` |
| `Fan` | `fan` | 14 | 23 | 31 | `on_command(FanCommand)` |
| `Light` | `light` | 15 | 24 | 32 | `on_command(LightCommand)` |
| `Sensor` | `sensor` | 16 | 25 | — | `publish_state(float)` |
| `Switch` | `switch` | 17 | 26 | 33 | `on_control(bool)` |
| `TextSensor` | `text_sensor` | 18 | 27 | — | `publish_state(string)` |
| `Camera` | `camera` | 43 | — | 45† | `on_image_request`, `send_image()` |
| `Climate` | `climate` | 46 | 47 | 48 | `on_command(ClimateCommand)` |
| `Number` | `number` | 49 | 50 | 51 | `on_control(float)` |
| `Select` | `select` | 52 | 53 | 54 | `on_control(string)` |
| `Lock` | `lock` | 58 | 59 | 60 | `on_command(uint32_t)` |
| `Button` | `button` | 61 | — | 62 | `on_press()` |
| `MediaPlayer` | `media_player` | 63 | 64 | 65 | `on_command(MediaPlayerCommand)` |
| `AlarmControlPanel` | `alarm_control_panel` | 94 | 95 | 96 | `on_command(cmd, code)` |
| `Text` | `text` | 97 | 98 | 99 | `on_control(string)` |
| `Date` | `date` | 100 | 101 | 102 | `on_control(y,m,d)` |
| `TimeEntity` | `time` | 103 | 104 | 105 | `on_control(h,m,s)` |
| `Event` | `event` | 107 | 108‡ | — | `publish_event(type)` |
| `Valve` | `valve` | 109 | 110 | 111 | `on_command(ValveCommand)` |
| `DateTime` | `datetime` | 112 | 113 | 114 | `on_control(epoch)` |
| `Update` | `update` | 116 | 117 | 118 | `on_command(uint32_t)` |

† `CameraImageRequest` (45) has no entity key; all `Camera` entities are notified.
‡ `Event` pushes `EventResponse` (108), not a subscribed initial state.

**Command structs** (`CoverCommand`, `FanCommand`, `LightCommand`,
`ClimateCommand`, `MediaPlayerCommand`, `ValveCommand`) mirror the optional
fields in the corresponding `*CommandRequest` protobuf messages (`has_*` flags
+ values).

**Not an entity:** `ListEntitiesServicesResponse` (41) / `ExecuteServiceRequest`
(42) — user-defined HA services callable from firmware — are **not** implemented.

### 6.4 Event & Camera

**Event** — stateless from HA's subscription perspective (`state_type() == 0`).
When something happens on-device, call:

```cpp
motion_event.publish_event("motion");  // -> EventResponse to subscribers
```

**Camera** — discovery only until HA requests an image:

```cpp
cam.on_image_request = [](bool single, bool stream) {
  // capture frame, then:
  cam.send_image(jpeg_bytes, jpeg_len, true /* done */);
};
```

`send_image()` may be called multiple times per request (set `done = false` on
intermediate chunks if splitting large JPEGs).

---

## 7. Protobuf codec

Self-contained proto3 encoder/decoder in `protobuf.h`. No `protoc`/nanopb. All
encoders follow proto3 semantics: **default scalar values are omitted** unless
`force = true`.

### `ProtoWriteBuffer`

```cpp
class ProtoWriteBuffer {
  std::vector<uint8_t> data;
  void clear();
  size_t size() const;

  void encode_uint32 (uint32_t field, uint32_t value, bool force = false);
  void encode_int32  (uint32_t field, int32_t  value, bool force = false); // sign-extended
  void encode_sint32 (uint32_t field, int32_t  value, bool force = false); // ZigZag
  void encode_bool   (uint32_t field, bool     value, bool force = false);
  void encode_enum   (uint32_t field, uint32_t value, bool force = false);
  void encode_string (uint32_t field, const std::string &value, bool force = false);
  void encode_bytes  (uint32_t field, const uint8_t *ptr, size_t len, bool force = false);
  void encode_fixed32(uint32_t field, uint32_t value, bool force = false); // little-endian
  void encode_float  (uint32_t field, float    value, bool force = false);
  void encode_repeated_string(uint32_t field, const std::vector<std::string> &values);
  void encode_repeated_uint32(uint32_t field, const std::vector<uint32_t> &values);

  // Low-level
  void write_byte(uint8_t b);
  void write_varint(uint64_t value);
  void write_tag(uint32_t field, WireType wire);
};
```

### `ProtoDecoder` / `ProtoField`

```cpp
struct ProtoField {
  uint32_t field, wire;
  uint64_t as_varint;           // wire == WIRE_VARINT
  uint32_t as_fixed32;          // wire == WIRE_FIXED32
  uint64_t as_fixed64;          // wire == WIRE_FIXED64
  const uint8_t *as_bytes;      // wire == WIRE_LEN
  size_t bytes_len;

  bool         as_bool()   const;
  std::string  as_string() const;
  float        as_float()  const;   // reinterpret fixed32
};

class ProtoDecoder {
  ProtoDecoder(const uint8_t *data, size_t len);
  bool next(ProtoField &out);   // false at end / on malformed input
};
```

Typical decode:

```cpp
ProtoDecoder dec(payload, len);
ProtoField f;
uint32_t key = 0; bool state = false;
while (dec.next(f)) {
  if (f.field == 1 && f.wire == WIRE_FIXED32) key = f.as_fixed32;
  if (f.field == 2 && f.wire == WIRE_VARINT)  state = f.as_bool();
}
```

### Free functions

```cpp
void append_varint(std::vector<uint8_t> &out, uint64_t value);    // frame headers
bool stream_varint(const uint8_t *data, size_t len, size_t &pos, uint32_t &out);
```

`stream_varint()` returns `false` when the buffer is too short (caller should
wait for more bytes) — used by the frame parser.

### Wire types

```cpp
enum WireType { WIRE_VARINT = 0, WIRE_FIXED64 = 1, WIRE_LEN = 2, WIRE_FIXED32 = 5 };
```

---

## 8. FNV-1 hash

```cpp
uint32_t fnv1_hash(const std::string &object_id);
```

32-bit FNV-1 (offset basis `2166136261`, prime `16777619`, **multiply then
XOR** — not FNV-1a). Used to derive each entity's `key`. Validated vector:
`fnv1_hash("kmeter_temperature") == 0xDB0A35EC`.

---

## 9. Message type IDs

`api_message_types.h` defines the `MessageType` enum and:

```cpp
constexpr uint32_t API_VERSION_MAJOR = 1;
constexpr uint32_t API_VERSION_MINOR = 10;
```

### Handled by this library

| Category | IDs | Messages |
|---|---|---|
| Session | 1–8 | Hello, Connect, Disconnect, Ping |
| Device | 9–10 | DeviceInfo request/response |
| Discovery | 11, 12–18, 43, 46, 49, 52, 58, 61, 63, 94, 97, 100, 103, 107, 109, 112, 116, **19** | All `ListEntities*` + Done |
| State subscribe | 20 | SubscribeStates |
| State push | 21–27, 47, 50, 53, 59, 64, 95, 98, 101, 104, 110, 113, 117, **108** | All `*StateResponse` + `EventResponse` |
| Commands | 30–33, 48, 51, 54, 60, 62, 65, 96, 99, 102, 105, 111, 114, 118 | All entity `*CommandRequest` |
| Camera | 44–45 | CameraImage response/request |
| Logs | 28–29 | SubscribeLogs request; SubscribeLogsResponse (push) |
| HA services | 34–35 | SubscribeHomeassistantServices; HomeassistantServiceResponse (push) |
| Time | 36–37 | GetTime request/response (setup + post-connect sync) |
| HA states | 38–40 | SubscribeHomeAssistantStates; SubscribeHomeAssistantStateResponse; HomeAssistantStateResponse |

### Defined but not handled

| IDs | Messages |
|---|---|
| 41–42 | User-defined services (`ListEntitiesServices` / `ExecuteService`) |
| 66+ | Bluetooth proxy, voice assistant, … |

Unknown inbound message types are **ignored** (connection stays alive). The
canonical full schema is in [`../api.proto`](../api.proto).

---

## 10. Runtime model & lifecycle

- **Single-threaded / cooperative.** Everything runs from the Arduino `loop()`
  via `APIServer::loop()`. No tasks, no locks. `on_control`/`on_press`
  callbacks and `publish_state()` run in that same context.
- **Non-blocking reads.** Each `loop()` drains a bounded number of socket
  chunks per connection, buffering partial frames until complete.
- **Multiple clients.** Several Home Assistant connections are supported
  simultaneously; each maintains its own subscription/auth state.

### Transport selection (per client)

The first byte the client sends selects the transport:

```
first byte 0x00 ─ plaintext ─ (rejected if encryption_key is set)
first byte 0x01 ─ Noise     ─ (rejected if no encryption_key)
```

Noise sub-states (responder): `CLIENT_HELLO` → `HANDSHAKE` → `READY`.
On `CLIENT_HELLO` the device sends its server hello and builds the prologue
(`"NoiseAPIInit"` + uint16_be(len) + client hello). On `HANDSHAKE` it reads the
initiator's `e`/psk message and writes its `e, ee` reply, then `split()`s into
transport keys. From `READY` on, every frame is `0x01` + uint16_be(len) +
ChaCha20-Poly1305 ciphertext of `uint16_be(type) + uint16_be(len) + payload`.

### Application state machine (per client, after transport is up)

```
WAIT_HELLO ──HelloRequest──> WAIT_CONNECT ──ConnectRequest(ok)──> CONNECTED
                                   │
                              (bad password) ──> ConnectResponse(invalid) + close
```

The lifecycle is identical for plaintext and Noise — it operates on decrypted
messages. Before authentication only Hello/Connect/Ping/Disconnect are accepted;
any privileged message pre-auth closes the connection.

### Handled client requests (after auth)

`DeviceInfo`, `ListEntities`, `SubscribeStates`, all entity `*CommandRequest`
types (see §9), and `CameraImageRequest`. Outbound: matching responses, initial
state dump on subscribe, `PingRequest` keepalives, and pushed state/event/camera
frames.

### Crypto stack (Noise only)

Implemented in `crypto/` — vendored TweetNaCl (X25519), in-tree SHA-256 +
HMAC + HKDF, ChaCha20-Poly1305 (RFC 8439). `NoiseResponder` in `noise.h`
implements the device-side `Noise_NNpsk0_25519_ChaChaPoly_SHA256` pattern.
Compiled automatically with the library (`tweetnacl.c` + `tweetnacl_glue.cpp`).

### Keepalive (compile-time constants in `api_connection.cpp`)

| Constant | Default | Meaning |
|---|---|---|
| `PING_INTERVAL_MS` | 20000 | server sends `PingRequest` this often |
| `KEEPALIVE_TIMEOUT_MS` | 90000 | drop the connection after this much inbound silence |
| `MAX_FRAME_PAYLOAD` | 8192 | reject larger frames (defensive) |

---

## 11. Extending the library

### Add a new entity type (if ESPHome adds one to the protocol)

1. Subclass `Entity` in `entity_types.h`.
2. Implement `list_type()`, `encode_list()`, `state_type()`, `encode_state()`,
   `kind()`. Use `encode_common(b)` plus type-specific fields per
   [`../api.proto`](../api.proto).
3. Add `publish_state(...)` (or equivalent) that calls `notify_changed()`.
4. Override `handle_command()` if the entity accepts commands; decode fields
   with `ProtoDecoder` and invoke a user callback.
5. Add the message IDs to `api_message_types.h`.
6. Add the command ID to the `switch` in `APIConnection::handle_message()` (if
   not already covered by the unified command block).
7. Add `add_xxx()` to `APIServer` if desired.

### Add a non-entity protocol feature (e.g. log streaming)

Extend the `switch` in `APIConnection::handle_message()`; decode with
`ProtoDecoder`, respond with `send_message(type, body)`.

---

## Open Items / Limitations

Tracking list of what is **not** implemented or known-limited in `0.4.1`.
Items are labeled by impact.

### Transport / security

- **[LOW] Plaintext mode is unauthenticated/unencrypted.** When `encryption_key`
  is left blank the device accepts plaintext (`0x00`). Set an `encryption_key`
  to require Noise. *Do not expose a plaintext device directly to the internet.*
- **[LOW] No rate limiting / lockout** on bad application passwords or repeated
  failed handshakes.
- **[LOW] Ephemeral RNG quality depends on the platform.** ESP32 uses
  `esp_fill_random` (hardware RNG); ESP8266 reads the hardware RNG register;
  the host build uses `/dev/urandom`. Ensure the ESP RF/WiFi subsystem is active
  for good entropy on-device.
- **[LOW] No TLS / alternative transport** — port 6053 TCP only.

> **Noise encryption is implemented** (`Noise_NNpsk0_25519_ChaChaPoly_SHA256`)
> using vendored TweetNaCl (X25519) plus in-tree SHA-256/HKDF and
> ChaCha20-Poly1305. All primitives are verified against published test vectors
> and the handshake interoperates with the `noiseprotocol` reference client.

### Protocol coverage

- **[LOW] Entity metadata is partial on some types.** Not every optional
  `ListEntities*` field from the proto is exposed via setters (e.g. climate fan/
  swing mode lists). Defaults are sensible for HA discovery.
- **[LOW] No user-defined services** (`ListEntitiesServicesResponse` /
  `ExecuteServiceRequest`).

> **Home Assistant integration helpers are implemented** — the device can call
> HA services / fire events (`call_homeassistant_service()`,
> `fire_homeassistant_event()`) and track HA entity states
> (`subscribe_homeassistant_state()`). See the "Home Assistant integration"
> subsection under the `APIServer` API.
- **[LOW] No Bluetooth Proxy or Voice Assistant** subsystems.
- **[LOW] No entity `device_id` / sub-device / area** assignment.
- **[LOW] Camera has no capture pipeline** — only the API hooks (`on_image_request`,
  `send_image()`); JPEG capture/streaming is application code.
- **[LOW] API version is advertised as 1.10.** All standard entity messages are
  implemented, plus logs, time sync, and HA service/state helpers; remaining
  integration subsystems (user-defined services, BLE proxy, voice) are not.

### Robustness / behavior

- **[MED] No backpressure handling.** `client_.write()` may block briefly if the
  TCP send buffer is full; very large entity counts produce many frames in one
  `loop()` pass.
- **[LOW] Heap usage.** `std::vector`/`std::string` are used throughout for
  clarity. Comfortable on ESP32; fine for a handful of entities on ESP8266, but
  not optimized for very constrained RAM or hundreds of entities.
- **[LOW] Persistence is switch-only.** `Switch` supports `restore_mode`
  (persisted via `platform_nvs.h`); other stateful entities (light, fan,
  number, …) are not yet persisted across reboot.
- **[LOW] Keepalive thresholds are compile-time constants**, not configurable at
  runtime.
- **[LOW] No built-in mDNS/zeroconf advertisement.** The library does not call
  `ESPmDNS`/`ESP8266mDNS` — WiFi must be up first and the node name is
  application configuration. Use [`examples/_common/api_mdns.h`](examples/_common/api_mdns.h)
  (`advertise_api_mdns()`) after `api.begin()`; all entity examples include it.
  Validated by compliance tests TS-15.4 / TS-15.5.

### Tooling / tests

- **[LOW] Host unit tests** (`test/host_test.cpp`) cover the portable core only
  (codec, FNV-1, entity encoding, switch `restore_mode` policy).
- **Host simulator** (`test/host_sim/`) compiles the real `api_server.cpp` /
  `api_connection.cpp` with POSIX WiFi shims and runs against the Python test bed
  on `localhost:6053` (plaintext or Noise via `SIM_ENCRYPTION_KEY`). See
  [`../TEST_STRATEGY.md`](../TEST_STRATEGY.md).

### Suggested roadmap

1. ~~mDNS advertisement for HA auto-discovery.~~ → see `examples/_common/api_mdns.h`
2. Bluetooth proxy / camera streaming helpers.
3. Persistence for other stateful entities (light, fan, number, …).
(Done: ✅ Noise transport; ✅ all standard entity types; ✅ log streaming; ✅ time sync;
✅ Serial → `api.log()` bridge; ✅ Home Assistant service/state helpers;
✅ switch state persistence (`restore_mode`).)
