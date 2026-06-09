# ESPHome Native API — Implementation Guide

This guide supplements [README.md](README.md) with the details needed to **implement a working client** without reading the ESPHome source code.

**Target API version:** 1.10  
**Canonical schemas:** [api.proto](api.proto), [api_options.proto](api_options.proto)

---

## Table of Contents

1. [Required Files in This Folder](#1-required-files-in-this-folder)
2. [External References](#2-external-references)
3. [Entity Key Hash (FNV-1)](#3-entity-key-hash-fnv-1)
4. [Noise Handshake — Client Implementation](#4-noise-handshake--client-implementation)
5. [Application Messages After Encryption](#5-application-messages-after-encryption)
6. [Core Entity Message Schemas](#6-core-entity-message-schemas)
7. [Minimal Client State Machine](#7-minimal-client-state-machine)
8. [Worked Example: kmeter Sensor Read](#8-worked-example-kmeter-sensor-read)
9. [Protobuf Encoding Quick Reference](#9-protobuf-encoding-quick-reference)
10. [Implementation Checklist](#10-implementation-checklist)

---

## 1. Required Files in This Folder

| File | Purpose |
|---|---|
| [README.md](README.md) | Protocol overview, message ID catalog, session flow |
| [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md) | This document — implementation details |
| [api.proto](api.proto) | Full protobuf message definitions (API 1.10) |
| [api_options.proto](api_options.proto) | Message ID and metadata extensions |

Generate language bindings from `api.proto` with `protoc`, or hand-encode the subset in §6 for minimal clients.

---

## 2. External References

| Resource | URL |
|---|---|
| ESPHome official protocol details (framing, Noise states, hex examples) | https://developers.esphome.io/architecture/api/protocol_details/ |
| Noise Protocol Framework | https://noiseprotocol.org/noise.html |
| Reference Node.js client (Noise + full protocol) | https://github.com/hjdhjd/esphome-client |
| ESPHome plaintext Python client (CLI, legacy subset) | https://github.com/esphome/esphome/tree/dev/esphome/api/client.py |
| Home Assistant integration (production client) | https://github.com/home-assistant/core/tree/dev/homeassistant/components/esphome |

---

## 3. Entity Key Hash (FNV-1)

Every entity has an `object_id` (string) and a `key` (fixed32). All runtime state and command messages use **`key` only**.

ESPHome computes `key` with **FNV-1 32-bit**:

```
offset_basis = 2166136261   (0x811C9DC5)
fnv_prime    = 16777619     (0x01000193)

hash = offset_basis
for each byte b in object_id (UTF-8):
    hash = (hash * fnv_prime) XOR b
    hash = hash AND 0xFFFFFFFF   // keep 32-bit
```

### Reference implementations

**Python:**
```python
def fnv1_hash(object_id: str) -> int:
    h = 2166136261
    for b in object_id.encode("utf-8"):
        h = ((h * 16777619) ^ b) & 0xFFFFFFFF
    return h
```

**C:**
```c
uint32_t fnv1_hash(const char *str) {
  uint32_t hash = 2166136261UL;
  for (; *str; str++) {
    hash *= 16777619UL;
    hash ^= (uint8_t)*str;
  }
  return hash;
}
```

### kmeter example hashes

| object_id | key (hex) | key (decimal) |
|---|---|---|
| `kmeter_temperature` | `0xDB0A35EC` | 3674879468 |
| `kmeter_internal_temperature` | `0x50E62F2A` | 1357262634 |
| `wifi_signal_sensor` | `0x8802176A` | 2281838442 |
| `restart_kmeter` | `0x67524C0B` | 1733446667 |

**Client note:** You normally read `key` from `ListEntities*Response` and never compute it yourself. Server authors and debug tools need FNV-1.

---

## 4. Noise Handshake — Client Implementation

### 4.1 Prerequisites

- TCP connect to device IP, port **6053**
- Enable **TCP_NODELAY**
- PSK: base64-decode the YAML `api.encryption.key` → **32 raw bytes**
- Noise pattern: **`Noise_NNpsk0_25519_ChaChaPoly_SHA256`**
- Your role: **`NOISE_ROLE_INITIATOR`**
- ESPHome role: **`NOISE_ROLE_RESPONDER`**

### 4.2 Complete client-side sequence

```
Phase A — Transport (cleartext on TCP wire, before any protobuf)
──────────────────────────────────────────────────────────────
1. TCP connect
2. Build Noise HandshakeState (initiator, PSK set)
3. Client transport hello frame (see 4.3)
4. Read server transport hello frame (see 4.4)
5. Set Noise prologue (see 4.5), start handshake
6. Loop: read/write Noise handshake frames until SPLIT
7. Split into send_cipher + recv_cipher

Phase B — Application (inside Noise DATA frames, encrypted)
──────────────────────────────────────────────────────────
8.  Send HelloRequest (type 1)
9.  Recv HelloResponse (type 2)
10. Send ConnectRequest (type 3)
11. Recv ConnectResponse (type 4)
12. ... normal API traffic ...
```

**Critical:** Steps 8–12 are **not** sent on cleartext TCP. They are protobuf payloads inside encrypted Noise DATA frames (§5).

### 4.3 Step 3 — Client transport hello (first bytes on TCP)

The client sends the **first Noise handshake message** as the client-hello payload:

```
1. noise_handshakestate_write_message() → payload (typically ~32 bytes, ephemeral key)
2. Frame on wire:
   [0x01] [uint16_be(len(payload))] [payload]
```

Example structure (payload length varies):
```
01 00 20  <32 bytes Noise message>
^  ^^^^^
|  length BE
indicator
```

The server ignores the payload content for now but includes it in the Noise prologue (§4.5).

### 4.4 Step 4 — Server transport hello

Read frame: `[0x01][uint16_be(size)][body]`

Body layout:
```
[0x01] [node_name\0] [mac_address\0]
```

Example (node=`kmeter`, mac=`AA:BB:CC:DD:EE:FF`):
```
01 6B 6D 65 74 65 72 00 41 41 3A 42 42 3A 43 43 3A 44 44 3A 45 45 3A 46 46 00
^  k  m  e  t  e  r  \0  A  A  :  B  B  :  C  C  :  D  D  :  E  E  :  F  F  \0
protocol=0x01
```

### 4.5 Step 5 — Noise prologue

Before `noise_handshakestate_start()`, set prologue to:

```
"NoiseAPIInit"           // 12 bytes, ASCII, no null terminator
uint16_be(client_hello_payload_length)
client_hello_payload     // exact bytes from step 3 body (NOT including frame header)
```

Python pseudocode:
```python
prologue = b"NoiseAPIInit"
prologue += len(client_hello_payload).to_bytes(2, "big")
prologue += client_hello_payload
handshake.set_prologue(prologue)
handshake.start()
```

Then continue the standard Noise NNpsk0 message exchange.

### 4.6 Steps 6–7 — Noise handshake frames

Each handshake frame on the wire:
```
[0x01] [uint16_be(body_len)] [body]
```

Body layout:
```
[0x00 = success] [noise_handshake_message_bytes...]
```

- **Send:** prepend `0x00` to each `write_message()` output before framing
- **Receive:** verify byte 0 is `0x00`; pass bytes 1..N to `read_message()`
- **Max body size during handshake:** 128 bytes
- **On error:** server sends `[0x01][reason_string...]` then closes

After `NOISE_ACTION_SPLIT`:
- **Initiator send_cipher** → encrypts client→server DATA frames
- **Initiator recv_cipher** → decrypts server→client DATA frames

### 4.7 PSK handling

```python
import base64
psk = base64.b64decode(yaml_encryption_key)  # must be exactly 32 bytes
assert len(psk) == 32
```

---

## 5. Application Messages After Encryption

Once in Noise **DATA** state, every protobuf message uses this framing:

### Wire format
```
[0x01] [uint16_be(encrypted_len)] [ChaChaPoly ciphertext + 16-byte MAC]
```

### Plaintext inside ciphertext (before encryption)
```
[uint16_be(message_type)] [uint16_be(protobuf_len)] [protobuf_bytes] [zero padding]
```

The MAC covers the encrypted portion. The 2-byte `encrypted_len` on the wire is **not** encrypted.

### Building an outbound message

```python
def build_inner(message_type: int, protobuf: bytes) -> bytes:
    return (
        message_type.to_bytes(2, "big")
        + len(protobuf).to_bytes(2, "big")
        + protobuf
    )

def encrypt_and_frame(send_cipher, inner: bytes) -> bytes:
    ciphertext = send_cipher.encrypt(inner)  # includes 16-byte MAC
    return b"\x01" + len(ciphertext).to_bytes(2, "big") + ciphertext
```

### Parsing an inbound message

```python
def read_frame(recv_cipher, data: bytes) -> tuple[int, bytes]:
    assert data[0] == 0x01
    enc_len = int.from_bytes(data[1:3], "big")
    inner = recv_cipher.decrypt(data[3:3+enc_len])
    msg_type = int.from_bytes(inner[0:2], "big")
    msg_len  = int.from_bytes(inner[2:4], "big")
    payload  = inner[4:4+msg_len]
    return msg_type, payload
```

---

## 6. Core Entity Message Schemas

These four entity types cover the kmeter device (sensors + restart button). Full definitions are in [api.proto](api.proto).

### 6.1 Sensor (type 16 list / 25 state)

**ListEntitiesSensorResponse (16)** — server → client

| Field | # | Type | Notes |
|---|---|---|---|
| object_id | 1 | string | e.g. `"kmeter_temperature"` |
| key | 2 | fixed32 | FNV-1 hash |
| name | 3 | string | `"KMeter Temperature"` |
| unique_id | 4 | string | HA registry ID |
| icon | 5 | string | optional |
| unit_of_measurement | 6 | string | e.g. `"°C"` |
| accuracy_decimals | 7 | int32 | e.g. `2` |
| force_update | 8 | bool | |
| device_class | 9 | string | e.g. `"temperature"` |
| state_class | 10 | enum | 0=none, 1=measurement, 2=total_increasing, 3=total |
| legacy_last_reset_type | 11 | enum | deprecated, ignore |
| disabled_by_default | 12 | bool | |
| entity_category | 13 | enum | 0=none, 1=config, 2=diagnostic |

**SensorStateResponse (25)** — server → client

| Field | # | Type | Notes |
|---|---|---|---|
| key | 1 | fixed32 | entity key |
| state | 2 | float | IEEE754 |
| missing_state | 3 | bool | true = no valid reading yet |

### 6.2 Binary Sensor (type 12 list / 21 state)

**ListEntitiesBinarySensorResponse (12)**

| Field | # | Type |
|---|---|---|
| object_id | 1 | string |
| key | 2 | fixed32 |
| name | 3 | string |
| unique_id | 4 | string |
| device_class | 5 | string |
| is_status_binary_sensor | 6 | bool |
| disabled_by_default | 7 | bool |
| icon | 8 | string |
| entity_category | 9 | enum |

**BinarySensorStateResponse (21)**

| Field | # | Type |
|---|---|---|
| key | 1 | fixed32 |
| state | 2 | bool |
| missing_state | 3 | bool |

### 6.3 Switch (type 17 list / 26 state / 33 command)

**ListEntitiesSwitchResponse (17)**

| Field | # | Type |
|---|---|---|
| object_id | 1 | string |
| key | 2 | fixed32 |
| name | 3 | string |
| unique_id | 4 | string |
| icon | 5 | string |
| assumed_state | 6 | bool |
| disabled_by_default | 7 | bool |
| entity_category | 8 | enum |
| device_class | 9 | string |

**SwitchStateResponse (26)**

| Field | # | Type |
|---|---|---|
| key | 1 | fixed32 |
| state | 2 | bool |

**SwitchCommandRequest (33)** — client → server

| Field | # | Type |
|---|---|---|
| key | 1 | fixed32 |
| state | 2 | bool |

### 6.4 Button (type 61 list / 62 command)

**ListEntitiesButtonResponse (61)**

| Field | # | Type |
|---|---|---|
| object_id | 1 | string |
| key | 2 | fixed32 |
| name | 3 | string |
| unique_id | 4 | string |
| icon | 5 | string |
| disabled_by_default | 6 | bool |
| entity_category | 7 | enum |
| device_class | 8 | string |

**ButtonCommandRequest (62)** — client → server

| Field | # | Type |
|---|---|---|
| key | 1 | fixed32 |

Button has **no state message** — it is fire-and-forget. Press by sending type 62 with the entity key.

---

## 7. Minimal Client State Machine

```
CONNECT_TCP
    ↓
NOISE_HANDSHAKE
    ↓
SEND_HELLO ──→ RECV_HELLO (check api_version_major == 1)
    ↓
SEND_CONNECT ──→ RECV_CONNECT (abort if invalid_password)
    ↓
AUTHENTICATED
    ↓
SEND_DEVICE_INFO_REQUEST ──→ RECV_DEVICE_INFO_RESPONSE (optional)
    ↓
SEND_LIST_ENTITIES ──→ RECV ListEntities* (collect keys) ──→ RECV ListEntitiesDone (19)
    ↓
SEND_SUBSCRIBE_STATES (20)
    ↓
LOOP:
    RECV *StateResponse (21, 25, 26, …)
    RECV PingRequest (7) → SEND PingResponse (8)
    SEND commands as needed
```

Messages you must handle in the receive loop for a sensor-only client:

| Type | Action |
|---|---|
| 2 | HelloResponse (during setup only) |
| 4 | ConnectResponse (during setup only) |
| 8 | PingResponse |
| 7 | PingRequest → reply with 8 |
| 10 | DeviceInfoResponse |
| 12–18, 61, … | ListEntities*Response — store key + metadata |
| 19 | ListEntitiesDoneResponse — discovery complete |
| 21, 25, 26, … | *StateResponse — update local state |
| 29 | SubscribeLogsResponse (if subscribed) |

---

## 8. Worked Example: kmeter Sensor Read

This traces a minimal encrypted session through the first `SensorStateResponse` for `kmeter_temperature`.

### 8.1 Assumptions

- Device: `kmeter`, encryption enabled, no password
- Client: `Bentuino`, API version 1.10
- Sensor reading: 23.5 °C
- object_id: `kmeter_temperature`, key: `0xDB0A35EC`

### 8.2 Phase A — Noise (abbreviated)

```
Client → Server:  01 00 20 <32-byte Noise msg>     # client transport hello
Server → Client:  01 00 1C 01 6B 6D 65 74 65 72 00 <mac>\0   # server hello
Client ↔ Server:  01 00 XX 00 <handshake msgs>    # Noise NNpsk0 exchange
                  ... until handshake complete ...
```

### 8.3 Phase B — HelloRequest (type 1)

**Protobuf payload** (14 bytes):
```
0A 08 42 65 6E 74 75 69 6E 6F  10 01  18 0A
└─ field 1: "Bentuino" ─────┘  └v1=1┘ └v2=10┘
```

Field breakdown:
- `0A 08` = field 1, wire type 2 (length-delimited), 8 bytes
- `42 65 6E 74 75 69 6E 6F` = "Bentuino"
- `10 01` = field 2 (api_version_major) = 1
- `18 0A` = field 3 (api_version_minor) = 10

**Inner frame (before encryption):**
```
00 01   type = 1
00 0E   length = 14
0A 08 42 65 6E 74 75 69 6E 6F 10 01 18 0A
```

**On wire:** `01` + uint16_be(encrypted_len) + ChaChaPoly(inner + MAC)

### 8.4 HelloResponse (type 2)

Server replies with type 2. Decode fields:
- `api_version_major` = 1
- `api_version_minor` = 10
- `server_info` = `"kmeter (esphome v…)"`
- `name` = `"kmeter"`

Reject if `api_version_major != 1`.

### 8.5 ConnectRequest (type 3) / ConnectResponse (type 4)

**ConnectRequest** with empty password → **0-byte protobuf** (proto3 omits default empty strings).

Inner frame: `00 03 00 00` (type 3, length 0)

**ConnectResponse** with success → typically `00 04 00 00` or single byte if `invalid_password=false` omitted.

Verify `invalid_password` is not true.

### 8.6 ListEntitiesRequest (type 11)

Empty message. Inner: `00 0B 00 00`

### 8.7 ListEntitiesSensorResponse (type 16)

Example protobuf (abbreviated):
```
12 14 6B 6D 65 74 65 72 5F 74 65 6D 70 65 72 61 74 75 72 65   # object_id
15 EC 35 0A DB                                                    # key fixed32
1A 12 4B 4D 65 74 65 72 20 54 65 6D 70 65 72 61 74 75 72 65   # name
...
```

Inner frame: `00 10` + length + protobuf

Store `key = 0xDB0A35EC` for this sensor.

### 8.8 ListEntitiesDoneResponse (type 19)

Empty protobuf. Signals discovery complete.

### 8.9 SubscribeStatesRequest (type 20)

Empty protobuf. Inner: `00 14 00 00`

Server will now push initial states for all entities.

### 8.10 SensorStateResponse (type 25) — 23.5 °C

**Protobuf payload** (10 bytes):
```
0D EC 35 0A DB   15 00 00 BC 41
└─ key=0xDB0A35EC ─┘ └─ float 23.5 ─┘
```

Field breakdown:
- `0D` = field 1, wire type 5 (fixed32) → key `EC 35 0A DB` (little-endian) = 0xDB0A35EC
- `15` = field 2, wire type 5 (fixed32) → `00 00 BC 41` = IEEE754 23.5

**Inner frame:**
```
00 19        message type 25
00 0A        protobuf length 10
0D EC 35 0A DB 15 00 00 BC 41
```

Decode: `state = 23.5`, `missing_state = false` (field 3 absent).

---

## 9. Protobuf Encoding Quick Reference

| Wire type | Code | Used for |
|---|---|---|
| Varint | 0 | bool, enum, int32, uint32 |
| Fixed64 | 1 | fixed64, sfixed64, double |
| Length-delimited | 2 | string, bytes, embedded messages |
| Fixed32 | 5 | fixed32, sfixed32, float |

**Tag byte** = `(field_number << 3) | wire_type`

Common examples:

| Encode | Bytes |
|---|---|
| field 1, empty string (omitted in proto3) | (nothing) |
| field 1, string `"hi"` | `0A 02 68 69` |
| field 2, uint32 `1` | `10 01` |
| field 1, fixed32 `0xDB0A35EC` | `0D EC 35 0A DB` |
| field 2, float `23.5` | `15 00 00 BC 41` |
| field 1, bool `true` | `08 01` |

---

## 10. Implementation Checklist

### Transport
- [ ] TCP connect port 6053, TCP_NODELAY
- [ ] Detect `0x01` indicator → Noise mode
- [ ] Base64-decode 32-byte PSK
- [ ] Complete Noise NNpsk0 handshake with correct prologue
- [ ] Encrypt/decrypt DATA frames with ChaChaPoly

### Session
- [ ] Send HelloRequest with client API version
- [ ] Validate HelloResponse major version
- [ ] Send ConnectRequest (password if configured)
- [ ] Check ConnectResponse.invalid_password
- [ ] Reply to PingRequest (7) with PingResponse (8)

### Entities
- [ ] Send ListEntitiesRequest (11)
- [ ] Parse ListEntities*Response messages, store type + key + metadata
- [ ] Wait for ListEntitiesDoneResponse (19)
- [ ] Send SubscribeStatesRequest (20)
- [ ] Handle SensorStateResponse (25) and other state types
- [ ] Send ButtonCommandRequest (62) / SwitchCommandRequest (33) using stored keys

### Robustness
- [ ] Handle unknown message types without crashing
- [ ] Timeout keepalive (reply to server ping within 150 s)
- [ ] Close TCP immediately on handshake/auth failure (no DisconnectRequest)

---

## License & Attribution

Derived from the [ESPHome](https://esphome.io) project (AGPL-3.0). Intended for Bentuino interoperability development.
