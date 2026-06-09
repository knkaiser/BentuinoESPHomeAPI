# ESPHome ↔ Home Assistant Native API Protocol Specification

This document describes the **native ESPHome API** used for communication between an ESPHome device (TCP **server**) and Home Assistant or other clients (TCP **clients**).

It is derived from the ESPHome implementation, primarily:

- `esphome/components/api/api.proto` — canonical message schema
- `esphome/components/api/api_frame_helper.cpp` — transport framing (plaintext + Noise)
- `esphome/components/api/api_connection.cpp` — connection lifecycle and session logic

**Document version:** 2025-06-08  
**Referenced server API version:** Major **1**, Minor **10**

---

## Documentation Set

| Document | Purpose |
|---|---|
| **[README.md](README.md)** (this file) | Protocol overview, message catalog, session flow |
| **[IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md)** | Step-by-step client implementation: Noise handshake, FNV-1 hash, core schemas, worked hex example |
| **[COMPLIANCE_TEST_PLAN.md](COMPLIANCE_TEST_PLAN.md)** | Conformance test suites, profiles, and results matrix for validating an implementation |
| **[TEST_STRATEGY.md](TEST_STRATEGY.md)** | Testing layers and architecture: how the test bed, firmware, and host simulator fit together |
| **[testbed/](testbed/README.md)** | Runnable Python test bed that executes the compliance tests (offline unit tests + online tests against a device) |
| **[firmware/](firmware/README.md)** | C++ device-side implementation of the protocol for ESP32/ESP8266 (PlatformIO), plaintext **and Noise encryption**, controllable by Home Assistant |
| **[firmware/REFERENCE.md](firmware/REFERENCE.md)** | API reference for the C++ `ESPHomeAPI` library, including open items / limitations |
| **[NATIVE_API_VS_MQTT.md](NATIVE_API_VS_MQTT.md)** | Comparison of Native API vs MQTT for Home Assistant integration (architecture, features, trade-offs) |
| **[api.proto](api.proto)** | Canonical protobuf message definitions (API 1.10) |
| **[api_options.proto](api_options.proto)** | Message ID and metadata extensions |

**Official ESPHome reference:** [Protocol Details — developers.esphome.io](https://developers.esphome.io/architecture/api/protocol_details/)

> **Implementing a client?** Start with IMPLEMENTATION_GUIDE.md — README alone is not sufficient without api.proto.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Protocol Stack](#2-protocol-stack)
3. [Transport Layer](#3-transport-layer)
4. [Application Layer — Connection Lifecycle](#4-application-layer--connection-lifecycle)
5. [Authentication & Access Control](#5-authentication--access-control)
6. [Keepalive](#6-keepalive)
7. [Message Type Catalog](#7-message-type-catalog)
8. [Typical Home Assistant Session Flow](#8-typical-home-assistant-session-flow)
9. [Entity Model](#9-entity-model)
10. [Disconnect Semantics](#10-disconnect-semantics)
11. [Configuration Reference](#11-configuration-reference)
12. [Implementation Notes for Client Authors](#12-implementation-notes-for-client-authors)
13. [Appendix: Frame Examples](#13-appendix-frame-examples)

---

## 1. Overview


| Property                | Value                                                                                |
| ----------------------- | ------------------------------------------------------------------------------------ |
| **Transport**           | TCP, default port **6053**                                                           |
| **Encoding**            | Protocol Buffers (proto3)                                                            |
| **Roles**               | ESPHome = server/responder; Home Assistant = client/initiator                        |
| **Security**            | Optional **Noise** transport encryption + optional **password** at application layer |
| **Current API version** | Major **1**, Minor **10** (advertised in `HelloResponse`)                            |


Home Assistant's `esphome` integration is the primary client. The ESPHome CLI (`esphome logs`) uses the same protocol, typically in plaintext mode.

### Version compatibility rules

- **Major mismatch** — breaking change in base protocol; client must disconnect immediately.
- **Minor mismatch** — breaking change in individual messages; client should log a warning and continue if possible.

---

## 2. Protocol Stack

```
┌─────────────────────────────────────────┐
│  Application layer (Protobuf messages)  │
│  Hello, Connect, entities, commands…    │
├─────────────────────────────────────────┤
│  Frame layer                            │
│  Plaintext (0x00) or Noise (0x01)       │
├─────────────────────────────────────────┤
│  TCP (port 6053, TCP_NODELAY enabled)   │
└─────────────────────────────────────────┘
```

### High-level connection sequence

```
Client (Home Assistant)              Server (ESPHome)
        |                                    |
        |======== TCP connect ==============|
        |                                    |
        |  [Optional Noise handshake]        |
        |                                    |
        |-------- HelloRequest (1) -------->|
        |<------- HelloResponse (2) --------|
        |-------- ConnectRequest (3) ------>|
        |<------- ConnectResponse (4) -------|
        |                                    |
        |======== Authenticated session ====|
        |  ListEntities, SubscribeStates,    |
        |  commands, state updates, pings    |
```

---

## 3. Transport Layer

The **first byte** on the wire identifies the transport mode.

### 3.1 Plaintext mode (legacy / CLI)

**Indicator byte:** `0x00`

Each application message is framed as:

```
[0x00] [VarInt: payload_length] [VarInt: message_type] [protobuf payload]
```


| Field          | Description                                                                     |
| -------------- | ------------------------------------------------------------------------------- |
| `VarInt`       | Standard protobuf variable-length integer (7 bits per byte, MSB = continuation) |
| `message_type` | Numeric ID mapping to a protobuf message (see §7)                               |
| `payload`      | Serialized protobuf message body                                                |


Both sides enable **TCP_NODELAY** (Nagle algorithm disabled).

If a plaintext client receives an unknown indicator byte, it may respond with a plaintext error frame starting with `0x00` so the remote knows plaintext is not supported.

### 3.2 Noise-encrypted mode (recommended)

**Indicator byte:** `0x01`

Configured in ESPHome YAML:

```yaml
api:
  encryption:
    key: <base64-encoded 32-byte PSK>
```

See **[IMPLEMENTATION_GUIDE.md §4](IMPLEMENTATION_GUIDE.md#4-noise-handshake--client-implementation)** for the complete client-side handshake algorithm, prologue construction, and cipher setup.

Summary:

1. **Client transport hello** — First TCP bytes: `0x01` + uint16_be(len) + first Noise `write_message()` output
2. **Server transport hello** — `0x01` + node_name `\0` + mac_address `\0`
3. **Noise prologue** — `"NoiseAPIInit"` + uint16_be(client hello len) + client hello payload
4. **Noise NNpsk0 exchange** — Frames with leading `0x00` success byte; max 128 bytes during handshake
5. **DATA phase** — Protobuf messages inside encrypted frames (below)

**Noise protocol identifier:** `Noise_NNpsk0_25519_ChaChaPoly_SHA256`

| Parameter    | Value                                                                               |
| ------------ | ----------------------------------------------------------------------------------- |
| ESPHome role | **Responder**                                                                       |
| Client role  | **Initiator**                                                                       |
| PSK          | 32-byte raw key (base64-decode YAML `encryption.key`)                               |
| Prologue     | `"NoiseAPIInit"` + client-hello length (2 bytes, big-endian) + client-hello payload |

**Official reference:** [ESPHome Protocol Details — Noise section](https://developers.esphome.io/architecture/api/protocol_details/#noise-protocol)

#### Encrypted data frames

After handshake completes, each frame on the wire:

```
[0x01] [uint16 BE: encrypted_body_length] [encrypted_body + 16-byte MAC]
```

Decrypted body layout:

```
[uint16 BE: message_type] [uint16 BE: payload_length] [protobuf payload] [zero padding]
```

The 16-byte ChaChaPoly MAC is appended to the ciphertext (not included in `encrypted_body_length` field on wire — the length covers ciphertext+MAC as one unit per ESPHome implementation).

---

## 4. Application Layer — Connection Lifecycle

Connection setup is a strict 4-step sequence. If any step fails, **both sides must close the TCP connection immediately** without sending `DisconnectRequest`.

### 4.1 HelloRequest (type 1)

**Direction:** Client → Server only, at connection start  
**Authentication:** Not required


| Field               | Type   | Description                                |
| ------------------- | ------ | ------------------------------------------ |
| `client_info`       | string | Client identifier, e.g. `"Home Assistant"` |
| `api_version_major` | uint32 | Client's supported API major version       |
| `api_version_minor` | uint32 | Client's supported API minor version       |


### 4.2 HelloResponse (type 2)

**Direction:** Server → Client only, at connection start  
**Authentication:** Not required


| Field               | Type   | Description                                     |
| ------------------- | ------ | ----------------------------------------------- |
| `api_version_major` | uint32 | Server API major (currently **1**)              |
| `api_version_minor` | uint32 | Server API minor (currently **10**)             |
| `server_info`       | string | Debug string, e.g. `"kmeter (esphome v2024.x)"` |
| `name`              | string | Device node name                                |


After sending `HelloResponse`, the server enters **CONNECTED** state.

### 4.3 ConnectRequest (type 3)

**Direction:** Client → Server only, after Hello  
**Authentication:** Not required (this *is* the authentication step)


| Field      | Type   | Description                                   |
| ---------- | ------ | --------------------------------------------- |
| `password` | string | API password; empty string if none configured |


### 4.4 ConnectResponse (type 4)

**Direction:** Server → Client only


| Field              | Type | Description                     |
| ------------------ | ---- | ------------------------------- |
| `invalid_password` | bool | `true` if authentication failed |


On success (`invalid_password = false`), server enters **AUTHENTICATED** state. Client should disconnect on failure.

---

## 5. Authentication & Access Control

Two independent security layers may be active:

1. **Transport encryption** — Noise PSK (when `api.encryption.key` is configured)
2. **Application password** — Plaintext password in `ConnectRequest` (when `api.password` is configured)

These are orthogonal: a device may use encryption only, password only, both, or neither.

### Server connection states


| State               | Meaning                                |
| ------------------- | -------------------------------------- |
| `WAITING_FOR_HELLO` | TCP connected, Hello not yet received  |
| `CONNECTED`         | Hello exchanged, not yet authenticated |
| `AUTHENTICATED`     | Connect succeeded with valid password  |


### Message access rules


| Access level                | Allowed messages                                                |
| --------------------------- | --------------------------------------------------------------- |
| **None** (always allowed)   | Hello, Connect, Disconnect, Ping                                |
| **Setup** (Hello completed) | `DeviceInfoRequest`, `GetTimeRequest`, `GetTimeResponse`        |
| **Authenticated**           | All other messages (entity list, commands, subscriptions, etc.) |


Access violations cause immediate disconnect (`on_fatal_error`).

Password comparison on the server uses constant-time logic over both length and content.

---

## 6. Keepalive


| Parameter                             | Value                            |
| ------------------------------------- | -------------------------------- |
| Idle timeout before server ping       | **60 seconds**                   |
| Disconnect if no ping response        | **150 seconds** (2.5× keepalive) |
| Client ping interval (Home Assistant) | ~**5 seconds**                   |


Either side may send `PingRequest` (7) at any time after connection setup. The recipient must reply with `PingResponse` (8).

The server tracks `last_traffic_` and resets it on any received packet. Ping responses from the client reset the idle timer.

---

## 7. Message Type Catalog

Each message has a fixed numeric **type ID**. Protobuf field definitions are in `api.proto`.

Legend: **C→S** = client to server, **S→C** = server to client, **Both** = either direction.

### 7.1 Core / session messages


| ID  | Message                  | Direction | Auth  |
| --- | ------------------------ | --------- | ----- |
| 1   | HelloRequest             | C→S       | No    |
| 2   | HelloResponse            | S→C       | No    |
| 3   | ConnectRequest           | C→S       | No    |
| 4   | ConnectResponse          | S→C       | No    |
| 5   | DisconnectRequest        | Both      | No    |
| 6   | DisconnectResponse       | Both      | No    |
| 7   | PingRequest              | Both      | No    |
| 8   | PingResponse             | Both      | No    |
| 9   | DeviceInfoRequest        | C→S       | Setup |
| 10  | DeviceInfoResponse       | S→C       | Setup |
| 11  | ListEntitiesRequest      | C→S       | Yes   |
| 19  | ListEntitiesDoneResponse | S→C       | Yes   |
| 20  | SubscribeStatesRequest   | C→S       | Yes   |


### 7.2 Entity discovery, state, and commands

For each entity type the server sends:

1. `**ListEntities*Response`** — metadata (during `ListEntitiesRequest` handling)
2. `***StateResponse**` — live state (after `SubscribeStatesRequest`, and on changes)

Commands reference entities by `**key**` (fixed32 hash), not `object_id`.


| ID (List / State / Cmd) | Entity type         | Command                         |
| ----------------------- | ------------------- | ------------------------------- |
| 12 / 21                 | Binary Sensor       | —                               |
| 13 / 22 / 30            | Cover               | CoverCommandRequest             |
| 14 / 23 / 31            | Fan                 | FanCommandRequest               |
| 15 / 24 / 32            | Light               | LightCommandRequest             |
| 16 / 25                 | Sensor              | —                               |
| 17 / 26 / 33            | Switch              | SwitchCommandRequest            |
| 18 / 27                 | Text Sensor         | —                               |
| 43 / 44 / 45            | Camera              | CameraImageRequest              |
| 46 / 47 / 48            | Climate             | ClimateCommandRequest           |
| 49 / 50 / 51            | Number              | NumberCommandRequest            |
| 52 / 53 / 54            | Select              | SelectCommandRequest            |
| 58 / 59 / 60            | Lock                | LockCommandRequest              |
| 61 / — / 62             | Button              | ButtonCommandRequest            |
| 63 / 64 / 65            | Media Player        | MediaPlayerCommandRequest       |
| 94 / 95 / 96            | Alarm Control Panel | AlarmControlPanelCommandRequest |
| 97 / 98 / 99            | Text                | TextCommandRequest              |
| 100 / 101 / 102         | Date                | DateCommandRequest              |
| 103 / 104 / 105         | Time                | TimeCommandRequest              |
| 107 / 108               | Event               | —                               |
| 109 / 110 / 111         | Valve               | ValveCommandRequest             |
| 112 / 113 / 114         | DateTime            | DateTimeCommandRequest          |
| 116 / 117 / 118         | Update              | UpdateCommandRequest            |


Not all entity types are present on every device; messages are compiled in only when the corresponding component is enabled (`USE_*` defines).

#### Common list-entity fields


| Field       | Type    | Description                             |
| ----------- | ------- | --------------------------------------- |
| `object_id` | string  | YAML entity ID                          |
| `key`       | fixed32 | Hash used in all state/command messages |
| `name`      | string  | Friendly name                           |
| `unique_id` | string  | Stable Home Assistant unique ID         |


#### State message notes

- `missing_state = true` means the entity has no valid reading yet (inverse of `has_state()`).
- State messages marked `no_delay` in the schema should be sent immediately, not batched.

#### Command message pattern

Commands use optional-field semantics:

```
bool has_state = 2;
bool state = 3;
bool has_brightness = 4;
float brightness = 5;
...
```

Only fields with `has_* = true` are applied. This allows partial updates.

### 7.3 DeviceInfoResponse (type 10)


| Field                            | Type   | Description                      |
| -------------------------------- | ------ | -------------------------------- |
| `uses_password`                  | bool   | Whether a password is configured |
| `name`                           | string | Node name                        |
| `friendly_name`                  | string | Human-friendly name              |
| `suggested_area`                 | string | Suggested Home Assistant area    |
| `mac_address`                    | string | e.g. `"AC:BC:32:89:0E:A9"`       |
| `esphome_version`                | string | e.g. `"2024.12.0"`               |
| `compilation_time`               | string | Build timestamp                  |
| `model`                          | string | Board model                      |
| `manufacturer`                   | string | e.g. `"Espressif"`               |
| `has_deep_sleep`                 | bool   | Deep sleep capability            |
| `project_name`                   | string | Optional project identifier      |
| `project_version`                | string | Optional project version         |
| `webserver_port`                 | uint32 | Web server port if enabled       |
| `legacy_bluetooth_proxy_version` | uint32 | BT proxy version                 |
| `bluetooth_proxy_feature_flags`  | uint32 | BT proxy features                |
| `bluetooth_mac_address`          | string | Bluetooth MAC                    |
| `legacy_voice_assistant_version` | uint32 | Voice assistant version          |
| `voice_assistant_feature_flags`  | uint32 | Voice assistant features         |


### 7.4 Logging


| ID  | Message               | Direction |
| --- | --------------------- | --------- |
| 28  | SubscribeLogsRequest  | C→S       |
| 29  | SubscribeLogsResponse | S→C       |


`SubscribeLogsRequest` fields:


| Field         | Type          | Description                           |
| ------------- | ------------- | ------------------------------------- |
| `level`       | LogLevel enum | Minimum level to stream (0–7)         |
| `dump_config` | bool          | Request full config dump on subscribe |


`LogLevel` values: `NONE=0`, `ERROR=1`, `WARN=2`, `INFO=3`, `CONFIG=4`, `DEBUG=5`, `VERBOSE=6`, `VERY_VERBOSE=7`

If a log line cannot fit in the TCP send buffer, the server sends `SubscribeLogsResponse` with `send_failed = true`.

### 7.5 Home Assistant integration helpers


| ID  | Message                               | Direction | Purpose                                                |
| --- | ------------------------------------- | --------- | ------------------------------------------------------ |
| 34  | SubscribeHomeassistantServicesRequest | C→S       | Subscribe to `homeassistant.service` calls from device |
| 35  | HomeassistantServiceResponse          | S→C       | Service/event payload from device to HA                |
| 38  | SubscribeHomeAssistantStatesRequest   | C→S       | Device requests HA entity state subscriptions          |
| 39  | SubscribeHomeAssistantStateResponse   | S→C       | Confirms each subscribed entity                        |
| 40  | HomeAssistantStateResponse            | C→S       | HA pushes entity state (and attribute) to device       |
| 36  | GetTimeRequest                        | Both      | Request current time                                   |
| 37  | GetTimeResponse                       | Both      | `epoch_seconds` (fixed32 Unix timestamp)               |


`HomeassistantServiceResponse` fields: `service`, `data[]`, `data_template[]`, `variables[]`, `is_event`.

Used by ESPHome automations calling `homeassistant.service`, `homeassistant.event`, and `homeassistant.tag_scanned`.

### 7.6 User-defined services


| ID  | Message                      | Direction |
| --- | ---------------------------- | --------- |
| 41  | ListEntitiesServicesResponse | S→C       |
| 42  | ExecuteServiceRequest        | C→S       |


Custom services defined in ESPHome YAML under `api.services`. Arguments use `ServiceArgType`:


| Value | Type         |
| ----- | ------------ |
| 0     | bool         |
| 1     | int (legacy) |
| 2     | float        |
| 3     | string       |
| 4     | bool[]       |
| 5     | int[]        |
| 6     | float[]      |
| 7     | string[]     |


Since API v1.3, integer arguments use **sint32** (ZigZag-encoded) in `ExecuteServiceArgument.int_`.

### 7.7 Bluetooth Proxy (optional)

Requires `bluetooth_proxy` component. Key message IDs:


| ID    | Message                                     | Direction |
| ----- | ------------------------------------------- | --------- |
| 66    | SubscribeBluetoothLEAdvertisementsRequest   | C→S       |
| 67    | BluetoothLEAdvertisementResponse            | S→C       |
| 68    | BluetoothDeviceRequest                      | C→S       |
| 69    | BluetoothDeviceConnectionResponse           | S→C       |
| 70    | BluetoothGATTGetServicesRequest             | C→S       |
| 71    | BluetoothGATTGetServicesResponse            | S→C       |
| 72    | BluetoothGATTGetServicesDoneResponse        | S→C       |
| 73    | BluetoothGATTReadRequest                    | C→S       |
| 74    | BluetoothGATTReadResponse                   | S→C       |
| 75    | BluetoothGATTWriteRequest                   | C→S       |
| 76–79 | GATT descriptor/notify messages             | Both      |
| 80    | SubscribeBluetoothConnectionsFreeRequest    | C→S       |
| 81    | BluetoothConnectionsFreeResponse            | S→C       |
| 82–88 | GATT errors, pairing, cache                 | Both      |
| 87    | UnsubscribeBluetoothLEAdvertisementsRequest | C→S       |
| 93    | BluetoothLERawAdvertisementsResponse        | S→C       |


Clients with API version < 1.7 receive legacy-format advertisement data fields.

### 7.8 Voice Assistant (optional)

Requires `voice_assistant` component. Key message IDs:


| a   | Message                             | Direction |
| --- | ----------------------------------- | --------- |
| 89  | SubscribeVoiceAssistantRequest      | C→S       |
| 90  | VoiceAssistantRequest               | S→C       |
| 91  | VoiceAssistantResponse              | C→S       |
| 92  | VoiceAssistantEventResponse         | C→S       |
| 106 | VoiceAssistantAudio                 | Both      |
| 115 | VoiceAssistantTimerEventResponse    | C→S       |
| 119 | VoiceAssistantAnnounceRequest       | C→S       |
| 120 | VoiceAssistantAnnounceFinished      | S→C       |
| 121 | VoiceAssistantConfigurationRequest  | C→S       |
| 122 | VoiceAssistantConfigurationResponse | S→C       |
| 123 | VoiceAssistantSetConfiguration      | C→S       |


---

## 8. Typical Home Assistant Session Flow

```
1. TCP connect (+ Noise handshake if encryption enabled)
2. HelloRequest  →  HelloResponse
3. ConnectRequest  →  ConnectResponse
4. DeviceInfoRequest  →  DeviceInfoResponse
5. ListEntitiesRequest
     ← ListEntities*Response (one per entity)
     ← ListEntitiesServicesResponse (if custom services exist)
     ← ListEntitiesDoneResponse
6. SubscribeStatesRequest
     ← *StateResponse (initial state for each entity)
     ← *StateResponse (ongoing updates on change)
7. SubscribeHomeassistantServicesRequest
8. SubscribeHomeAssistantStatesRequest (if device needs HA states)
9. Ongoing: commands, pings, log streaming, BT/voice if applicable
```

### Example: turning on a light

```
Client → LightCommandRequest (key=<hash>, has_state=true, state=true)
Server → LightStateResponse (key=<hash>, state=true, brightness=..., ...)
```

---

## 9. Entity Model

### State update model

1. Server pushes state when hardware changes (sensor read, GPIO toggle, etc.).
2. Client sends commands; server applies them and pushes updated state.
3. Messages marked `no_delay` in the schema bypass send buffering.
4. If the TCP send buffer is full, non-critical messages may be deferred (server uses a deduplicating deferred queue).

### Entity identification

- **`object_id`** — human-readable YAML ID (e.g. `"kmeter_temperature"`)
- **`key`** — fixed32 FNV-1 hash of `object_id`; used in all runtime state/command messages
- **`unique_id`** — stable identifier for Home Assistant entity registry

**Key hash algorithm:** FNV-1 32-bit (offset basis `2166136261`, prime `16777619`). See [IMPLEMENTATION_GUIDE.md §3](IMPLEMENTATION_GUIDE.md#3-entity-key-hash-fnv-1) for algorithm, code, and kmeter examples.

---

## 10. Disconnect Semantics

### Graceful shutdown

```
Initiator → DisconnectRequest (5)
Responder → DisconnectResponse (6)
Both parties close TCP socket
```

The initiator should wait for `DisconnectResponse` before closing (per schema comment on `DisconnectRequest`).

### Abnormal termination

During failed handshake, version mismatch, or authentication failure:

- **No** `DisconnectRequest` is sent
- TCP connection is closed immediately by both sides

---

## 11. Configuration Reference

ESPHome YAML:

```yaml
api:
  port: 6053                    # default
  password: ""                  # optional application-layer password
  encryption:
    key: !secret api_key        # base64-encoded 32-byte Noise PSK
  reboot_timeout: 15min          # reboot if no client connected this long
  services:                      # optional user-defined services
    - service: my_action
      variables:
        target: int
```

The encryption key is generated by ESPHome on first compile (or via `esphome secrets`). Home Assistant stores the same key in the device config entry.

---

## 12. Implementation Notes for Client Authors

1. **Detect transport mode** from the first received byte: `0x00` = plaintext, `0x01` = Noise.
2. **Always send Hello first**, including your API version; validate server's `api_version_major`.
3. **Authenticate** with Connect before any entity operations.
4. **Wait for `ListEntitiesDoneResponse`** before treating discovery as complete.
5. **Use `key` (fixed32)** in all state and command messages, not `object_id`.
6. **Handle unknown message types** gracefully — skip or log, do not crash.
7. **Respect minor version differences** — newer servers may send messages your client does not yet decode.
8. **Enable TCP_NODELAY** on the client socket.
9. **Implement keepalive** — reply to server pings; optionally send your own.
10. **Use the bundled schemas** — [api.proto](api.proto) and [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md) in this folder.
11. **Follow the worked example** — [IMPLEMENTATION_GUIDE.md §8](IMPLEMENTATION_GUIDE.md#8-worked-example-kmeter-sensor-read) traces a full session through the first sensor reading.

### Source files


| File | Purpose |
|---|---|
| [api.proto](api.proto) | Message definitions (bundled, API 1.10) |
| [api_options.proto](api_options.proto) | Message ID extensions (bundled) |
| [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md) | Client implementation guide |
| [ESPHome Protocol Details](https://developers.esphome.io/architecture/api/protocol_details/) | Official framing reference with hex examples |
| [hjdhjd/esphome-client](https://github.com/hjdhjd/esphome-client) | Reference Node.js client with Noise |
| `homeassistant/components/esphome` | Production Home Assistant client |

---

## 13. Appendix: Frame Examples

### Plaintext PingRequest (type 7, empty payload)

```
Hex:  00 00 07
      │  │  └── message type 7 (VarInt)
      │  └───── payload length 0 (VarInt)
      └──────── plaintext indicator
```

### Plaintext frame structure (general)

```
Offset  Field
──────  ─────
0       0x00 (indicator)
1..n    VarInt payload_length
n+1..m  VarInt message_type
m+1..   protobuf payload (payload_length bytes)
```

### Noise encrypted data frame (general)

```
Wire:
  0x01 | uint16_be(encrypted_len) | encrypted_body

Decrypted body:
  uint16_be(message_type) | uint16_be(payload_len) | payload | padding
```

### VarInt encoding example

Value 300 encodes as two bytes: `0xAC 0x02`

```
  300 = 0b100101100
  byte0 = (300 & 0x7F) | 0x80 = 0xAC  (continuation)
  byte1 = 300 >> 7       = 0x02
```

---

## License & Attribution

This specification documents the ESPHome native API protocol as implemented by the [ESPHome project](https://esphome.io) (AGPL-3.0). It is intended for interoperability and Bentuino development reference.

For the authoritative and always up-to-date message definitions, see the ESPHome source repository.