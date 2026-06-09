# ESPHome Native API — Compliance Test Plan

This test plan verifies that an implementation of the ESPHome native API (client or server) conforms to the protocol described in [README.md](README.md) and [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md).

**Target API version:** 1.10  
**Schemas under test:** [api.proto](api.proto), [api_options.proto](api_options.proto)

---

## Table of Contents

1. [Scope & Conformance Targets](#1-scope--conformance-targets)
2. [Test Environment](#2-test-environment)
3. [How to Read a Test Case](#3-how-to-read-a-test-case)
4. [TS-1: Transport — Plaintext Framing](#ts-1-transport--plaintext-framing)
5. [TS-2: Transport — Noise Encryption](#ts-2-transport--noise-encryption)
6. [TS-3: Connection Lifecycle](#ts-3-connection-lifecycle)
7. [TS-4: Authentication & Access Control](#ts-4-authentication--access-control)
8. [TS-5: Keepalive & Timeouts](#ts-5-keepalive--timeouts)
9. [TS-6: Entity Discovery](#ts-6-entity-discovery)
10. [TS-7: State Subscription & Updates](#ts-7-state-subscription--updates)
11. [TS-8: Commands](#ts-8-commands)
12. [TS-9: Message Encoding / Protobuf](#ts-9-message-encoding--protobuf)
13. [TS-10: Home Assistant Helpers](#ts-10-home-assistant-helpers)
14. [TS-11: Disconnect Semantics](#ts-11-disconnect-semantics)
15. [TS-12: Robustness & Negative Tests](#ts-12-robustness--negative-tests)
16. [TS-13: Interoperability](#ts-13-interoperability)
17. [TS-14: Logging](#ts-14-logging)
18. [TS-15: Home Assistant Pairing & Discovery](#ts-15-home-assistant-pairing--discovery)
19. [Conformance Profiles](#19-conformance-profiles)
20. [Results Matrix Template](#20-results-matrix-template)

---

## 1. Scope & Conformance Targets

An implementation may act as one or both roles. Each test is tagged with the role it applies to.

| Role | Description |
|---|---|
| **CLIENT** | Initiator (Home Assistant equivalent, Bentuino client) |
| **SERVER** | Responder (ESPHome device equivalent) |
| **BOTH** | Applies to either role |

### Requirement levels (RFC 2119)

- **MUST** — failure = non-conformant
- **SHOULD** — failure = warning, document deviation
- **MAY** — optional capability

### Out of scope

- Component-specific behavior (kmeter sensor accuracy, hardware timing)
- Wi-Fi/network provisioning

> **Note:** mDNS/zeroconf is normally a separate layer, but **TS-15.4/15.5**
> cover the `_esphomelib._tcp` advertisement and `api_encryption` TXT record
> because Home Assistant relies on them for discovery and encryption-key
> prompting. Those cases are **SHOULD** and are optional in automated runs unless
> `--mdns-name` is supplied.

---

## 2. Test Environment

### Required tooling

| Tool | Purpose |
|---|---|
| Reference ESPHome device (e.g. `kmeter`) | SERVER-under-test counterpart |
| Reference client ([esphome-client](https://github.com/hjdhjd/esphome-client) or HA) | CLIENT-under-test counterpart |
| Packet capture (Wireshark / `tcpdump`) | Wire-format verification |
| `protoc` + `api.proto` | Encode/decode validation |
| Noise test harness | PSK / handshake validation |
| A configurable test double | Inject malformed frames (TS-12) |

### Test fixtures

| Fixture | Value |
|---|---|
| Device name | `kmeter` |
| Encryption PSK | known 32-byte base64 key |
| Password (separate fixture) | `test-password` |
| Known entities | `kmeter_temperature` (sensor), `restart_kmeter` (button) |
| Known key | `kmeter_temperature` → `0xDB0A35EC` |

### Recommended setup

Run two device profiles to exercise both transports:
- **Profile A:** `api.encryption.key` set (Noise)
- **Profile B:** `api.password` set, no encryption (plaintext)

---

## 3. How to Read a Test Case

```
ID         Unique identifier (TS-x.y)
Role       CLIENT / SERVER / BOTH
Level      MUST / SHOULD / MAY
Precond    Required state before the test
Steps      Numbered actions
Expected   Observable pass criteria
Evidence   What to capture for the results matrix
```

---

## TS-1: Transport — Plaintext Framing

| ID | Role | Level | Description |
|---|---|---|---|
| TS-1.1 | BOTH | MUST | Frames begin with indicator byte `0x00` |
| TS-1.2 | BOTH | MUST | Payload length encoded as VarInt before type |
| TS-1.3 | BOTH | MUST | Message type encoded as VarInt after length |
| TS-1.4 | BOTH | MUST | Multi-byte VarInt (payload > 127 bytes) parses correctly |
| TS-1.5 | BOTH | MUST | Empty-payload message (length 0) handled (e.g. PingRequest) |
| TS-1.6 | BOTH | SHOULD | TCP_NODELAY enabled on socket |
| TS-1.7 | BOTH | MUST | Partial frame across TCP segments reassembled correctly |
| TS-1.8 | BOTH | MUST | Multiple frames in one TCP segment all processed |

### TS-1.4 (detail)

- **Precond:** Connected (plaintext).
- **Steps:** Trigger a message with a payload ≥ 128 bytes (e.g. a `DeviceInfoResponse` with long strings, or `ListEntitiesSensorResponse`).
- **Expected:** Length VarInt uses ≥ 2 bytes; receiver decodes full payload.
- **Evidence:** Packet capture showing multi-byte VarInt length.

### TS-1.7 (detail)

- **Steps:** Send a frame split into two TCP writes with a delay between header and payload.
- **Expected:** Receiver buffers and reassembles; no error, message delivered once.
- **Evidence:** Log showing single decoded message.

---

## TS-2: Transport — Noise Encryption

| ID | Role | Level | Description |
|---|---|---|---|
| TS-2.1 | BOTH | MUST | Frames begin with indicator byte `0x01` |
| TS-2.2 | CLIENT | MUST | Client transport hello = `0x01` + uint16_be(len) + Noise msg |
| TS-2.3 | SERVER | MUST | Server hello = `0x01` + size + `0x01` + name`\0` + mac`\0` |
| TS-2.4 | BOTH | MUST | Prologue = `"NoiseAPIInit"` + uint16_be(hello_len) + hello payload |
| TS-2.5 | BOTH | MUST | Handshake uses `Noise_NNpsk0_25519_ChaChaPoly_SHA256` |
| TS-2.6 | BOTH | MUST | PSK = base64-decoded key, exactly 32 bytes |
| TS-2.7 | BOTH | MUST | Handshake frames carry leading `0x00` success byte |
| TS-2.8 | BOTH | MUST | Handshake frame body ≤ 128 bytes enforced |
| TS-2.9 | BOTH | MUST | DATA frames: type+len+payload encrypted, ChaChaPoly MAC valid |
| TS-2.10 | SERVER | MUST | Wrong PSK → handshake fails, connection closed |
| TS-2.11 | SERVER | SHOULD | Explicit reject frame `0x01`+reason sent on handshake error |
| TS-2.12 | BOTH | MUST | MAC failure on DATA frame → immediate close, no error msg |
| TS-2.13 | CLIENT | MUST | Correct send/recv cipher direction after SPLIT |

### TS-2.6 (detail)

- **Steps:** Configure PSK; capture handshake; verify cipher init.
- **Expected:** Base64 key decodes to 32 raw bytes; handshake completes.
- **Fail case:** 31- or 33-byte key rejected before connect.

### TS-2.10 (detail)

- **Precond:** Server Profile A (Noise).
- **Steps:** Client attempts handshake with an incorrect PSK.
- **Expected:** `noise` MAC failure; server closes; client sees handshake failure.
- **Evidence:** Capture showing reject frame (TS-2.11) and TCP close.

### TS-2.12 (detail)

- **Steps:** After handshake, inject a DATA frame with a corrupted MAC.
- **Expected:** Receiver closes connection immediately and sends **no** error message (anti-information-leak).
- **Evidence:** No reject frame on wire; TCP FIN/RST.

---

## TS-3: Connection Lifecycle

| ID | Role | Level | Description |
|---|---|---|---|
| TS-3.1 | CLIENT | MUST | First app message is HelloRequest (type 1) |
| TS-3.2 | SERVER | MUST | Responds with HelloResponse (type 2) carrying API 1.10 |
| TS-3.3 | CLIENT | MUST | Sends ConnectRequest (type 3) after Hello |
| TS-3.4 | SERVER | MUST | Responds with ConnectResponse (type 4) |
| TS-3.5 | CLIENT | MUST | Rejects/disconnects on `api_version_major` mismatch |
| TS-3.6 | CLIENT | SHOULD | Warns (not disconnect) on `api_version_minor` mismatch |
| TS-3.7 | SERVER | MUST | Sends `name` field in HelloResponse (field 4) |
| TS-3.8 | BOTH | MUST | No app messages accepted before Hello completes (see TS-4) |
| TS-3.9 | CLIENT | MUST | HelloRequest includes `api_version_major`/`minor` |

### TS-3.5 (detail)

- **Steps:** Present a HelloResponse with `api_version_major = 2`.
- **Expected:** Client disconnects immediately (breaking change rule).
- **Evidence:** Connection closed without further messages.

### TS-3.2 (detail)

- **Expected fields:** `api_version_major = 1`, `api_version_minor = 10`, `server_info` non-empty, `name = "kmeter"`.

---

## TS-4: Authentication & Access Control

| ID | Role | Level | Description |
|---|---|---|---|
| TS-4.1 | SERVER | MUST | Hello/Connect/Disconnect/Ping allowed without auth |
| TS-4.2 | SERVER | MUST | DeviceInfo/GetTime allowed after Hello, before auth (Setup level) |
| TS-4.3 | SERVER | MUST | All other messages require AUTHENTICATED state |
| TS-4.4 | SERVER | MUST | Correct password → `invalid_password = false` |
| TS-4.5 | SERVER | MUST | Wrong password → `invalid_password = true` |
| TS-4.6 | SERVER | MUST | Pre-auth ListEntities/Subscribe → disconnect **when password is required** |
| TS-4.7 | SERVER | SHOULD | Password comparison is constant-time |
| TS-4.8 | SERVER | MUST | No-password device accepts empty ConnectRequest password |
| TS-4.9 | CLIENT | MUST | Aborts session if `invalid_password = true` |

### TS-4.6 (detail)

- **Precond:** Device uses a non-empty `api.password`; Hello done, Connect NOT sent (or wrong password).
- **Steps:** Send `ListEntitiesRequest` (type 11).
- **Expected:** Server triggers `on_no_setup_connection` / `on_unauthenticated_access` → fatal error → disconnect.
- **Evidence:** TCP close; optional debug log.
- **Exception:** Passwordless servers (empty password) **must not** apply this rule — see **TS-15.1**. Current Home Assistant / aioesphomeapi (ESPHome 2026.1+) may skip `ConnectRequest` on such devices and proceed directly to entity discovery after Hello.

### TS-4.7 (detail)

- **Method:** Statistical timing over many wrong-password attempts of varying length/content.
- **Expected:** No measurable correlation between time and password correctness/length.
- **Note:** SHOULD — document if not constant-time.

---

## TS-5: Keepalive & Timeouts

| ID | Role | Level | Description |
|---|---|---|---|
| TS-5.1 | SERVER | MUST | Sends PingRequest (7) after ~60 s idle |
| TS-5.2 | BOTH | MUST | Replies to PingRequest with PingResponse (8) |
| TS-5.3 | SERVER | MUST | Disconnects if no ping response within ~150 s |
| TS-5.4 | CLIENT | SHOULD | Sends periodic pings (~5 s HA-style) — optional cadence |
| TS-5.5 | BOTH | MUST | Any received traffic resets idle timer |
| TS-5.6 | SERVER | SHOULD | Ping retry/backoff on transient send failure |

### TS-5.3 (detail)

- **Steps:** Client connects, authenticates, then stops responding to pings.
- **Expected:** Server disconnects at ~2.5× keepalive (≈150 s).
- **Evidence:** Timestamped capture of last ping → disconnect.

---

## TS-6: Entity Discovery

| ID | Role | Level | Description |
|---|---|---|---|
| TS-6.1 | CLIENT | MUST | Sends ListEntitiesRequest (11) when authenticated |
| TS-6.2 | SERVER | MUST | Sends one `ListEntities*Response` per entity |
| TS-6.3 | SERVER | MUST | Each list response carries object_id, key, name, unique_id |
| TS-6.4 | SERVER | MUST | `key` equals FNV-1 of object_id |
| TS-6.5 | SERVER | MUST | Terminates discovery with ListEntitiesDoneResponse (19) |
| TS-6.6 | SERVER | MUST | Sends ListEntitiesServicesResponse (41) for user services |
| TS-6.7 | CLIENT | MUST | Treats discovery complete only after type 19 |
| TS-6.8 | SERVER | SHOULD | Omits entities marked internal |
| TS-6.9 | BOTH | MUST | Correct message-type IDs per entity (see catalog) |

### TS-6.4 (detail)

- **Steps:** Capture `ListEntitiesSensorResponse` for `kmeter_temperature`.
- **Expected:** `key` field = `0xDB0A35EC` (little-endian fixed32 on wire `EC 35 0A DB`).
- **Evidence:** Decoded field vs computed FNV-1.

### TS-6.2 (detail) — kmeter expectation

| Entity | Expected message |
|---|---|
| `kmeter_temperature` | ListEntitiesSensorResponse (16) |
| `kmeter_internal_temperature` | ListEntitiesSensorResponse (16) |
| `wifi_signal_sensor` | ListEntitiesSensorResponse (16) |
| `restart_kmeter` | ListEntitiesButtonResponse (61) |

---

## TS-7: State Subscription & Updates

| ID | Role | Level | Description |
|---|---|---|---|
| TS-7.1 | CLIENT | MUST | Sends SubscribeStatesRequest (20) |
| TS-7.2 | SERVER | MUST | Sends initial `*StateResponse` for each entity after subscribe |
| TS-7.3 | SERVER | MUST | Pushes `*StateResponse` on value change |
| TS-7.4 | SERVER | MUST | State references entity by `key` |
| TS-7.5 | SERVER | MUST | `missing_state = true` when no valid reading |
| TS-7.6 | SERVER | MUST | SensorStateResponse.state is IEEE754 float |
| TS-7.7 | CLIENT | MUST | Correlates state to entity via key from discovery |
| TS-7.8 | SERVER | SHOULD | `no_delay` messages flushed promptly |
| TS-7.9 | SERVER | MAY | Defers/dedups non-critical messages under buffer pressure |

### TS-7.6 (detail)

- **Steps:** Force a known sensor value (e.g. 23.5 °C); capture type 25.
- **Expected:** `state` field bytes = `00 00 BC 41` (fixed32 little-endian IEEE754 = 23.5).
- **Evidence:** Decoded float == fixture value.

---

## TS-8: Commands

| ID | Role | Level | Description |
|---|---|---|---|
| TS-8.1 | CLIENT | MUST | Command references entity by `key` (not object_id) |
| TS-8.2 | SERVER | MUST | Applies SwitchCommandRequest (33) and emits new state (26) |
| TS-8.3 | SERVER | MUST | ButtonCommandRequest (62) triggers action, no state msg |
| TS-8.4 | BOTH | MUST | `has_*` optional-field pattern honored (partial updates) |
| TS-8.5 | SERVER | SHOULD | Ignores command for unknown key without crashing |
| TS-8.6 | SERVER | MUST | Rejects commands when unauthenticated (TS-4.3) |
| TS-8.7 | CLIENT | MUST | LightCommandRequest sets only intended `has_*` fields |

### TS-8.3 (detail)

- **Steps:** Send `ButtonCommandRequest` (62) with key `0x67524C0B` (`restart_kmeter`).
- **Expected:** Device performs the button action (restart). No state response (buttons are stateless).
- **Evidence:** Device reboot observed; no type-related state echo.

### TS-8.4 (detail)

- **Steps:** Send `LightCommandRequest` with `has_brightness = true`, `brightness = 0.5`, all other `has_* = false`.
- **Expected:** Only brightness changes; state/color untouched.

---

## TS-9: Message Encoding / Protobuf

| ID | Role | Level | Description |
|---|---|---|---|
| TS-9.1 | BOTH | MUST | Field tags = `(field<<3)|wire_type` |
| TS-9.2 | BOTH | MUST | fixed32 fields little-endian (key, float) |
| TS-9.3 | BOTH | MUST | proto3 default values omitted from wire |
| TS-9.4 | BOTH | MUST | sint32 service args ZigZag-encoded (API ≥ 1.3) |
| TS-9.5 | BOTH | MUST | Unknown fields skipped, not fatal |
| TS-9.6 | BOTH | MUST | Strings UTF-8, length-delimited |
| TS-9.7 | BOTH | MUST | repeated fields decode fully |
| TS-9.8 | BOTH | SHOULD | Round-trip encode/decode is stable |

### TS-9.5 (detail)

- **Steps:** Inject a known message with an extra unknown field number.
- **Expected:** Decoder ignores the unknown field, processes known fields.
- **Rationale:** Forward compatibility with newer minor versions.

### TS-9.8 (detail)

- **Method:** Decode a captured message, re-encode, compare semantic equality (not byte equality — field order/defaults may vary).

---

## TS-10: Home Assistant Helpers

| ID | Role | Level | Description |
|---|---|---|---|
| TS-10.1 | SERVER | MAY | Sends HomeassistantServiceResponse (35) on `homeassistant.service` |
| TS-10.2 | CLIENT | MUST* | Subscribes via type 34 before expecting service calls |
| TS-10.3 | SERVER | MAY | `is_event = true` distinguishes events from service calls |
| TS-10.4 | BOTH | MAY | GetTime (36/37) exchange returns valid epoch |
| TS-10.5 | SERVER | MAY | SubscribeHomeAssistantStates (38/39/40) round-trips entity_id |
| TS-10.6 | SERVER | MAY | ExecuteServiceRequest (42) dispatches user service |

\* MUST only if the feature is supported by the implementation.

> **Implementation status:** the bundled firmware and Python test bed implement
> and exercise these helpers — service/event calls (34/35) via the `TS-13-HA-SVC`
> case, HA state subscriptions (38/39/40) via `TS-13-HA-STATE`, and the
> `GetTime` (36/37) exchange.

### TS-10.4 (detail)

- **Steps:** Trigger GetTimeRequest (36); respond with GetTimeResponse (37) containing `epoch_seconds`.
- **Expected:** Device clock set; value within tolerance of real time.

---

## TS-11: Disconnect Semantics

| ID | Role | Level | Description |
|---|---|---|---|
| TS-11.1 | BOTH | MUST | Graceful: DisconnectRequest (5) → DisconnectResponse (6) → close |
| TS-11.2 | BOTH | MUST | Initiator waits for response before closing |
| TS-11.3 | BOTH | MUST | After type 6, both sides close TCP |
| TS-11.4 | BOTH | MUST | Handshake/auth failure → immediate close, NO DisconnectRequest |
| TS-11.5 | SERVER | MUST | Network loss → immediate disconnect (no wait) |
| TS-11.6 | BOTH | SHOULD | Reboot timeout disconnect after configured idle |

### TS-11.4 (detail)

- **Steps:** Fail authentication (wrong password / bad handshake).
- **Expected:** No `DisconnectRequest` (5) on wire; TCP closed directly.
- **Evidence:** Capture confirms absence of type 5/6 during failure path.

---

## TS-12: Robustness & Negative Tests

| ID | Role | Level | Description |
|---|---|---|---|
| TS-12.1 | BOTH | MUST | Unknown message type → skipped, connection survives |
| TS-12.2 | BOTH | MUST | Bad indicator byte → connection closed / error frame |
| TS-12.3 | BOTH | MUST | Truncated VarInt → no crash, waits or closes cleanly |
| TS-12.4 | BOTH | MUST | Oversized declared length → bounded handling, no OOM |
| TS-12.5 | BOTH | MUST | Zero-length payload type handled |
| TS-12.6 | SERVER | MUST | Handshake frame > 128 bytes rejected (TS-2.8) |
| TS-12.7 | BOTH | MUST | Malformed protobuf → message dropped, no crash |
| TS-12.8 | BOTH | SHOULD | Rapid connect/disconnect churn doesn't leak resources |
| TS-12.9 | BOTH | MUST | Garbage bytes mid-stream → safe failure |
| TS-12.10 | SERVER | SHOULD | Many simultaneous connections handled or bounded |

### TS-12.2 (detail)

- **Plaintext steps:** Send a frame starting with `0x02`.
- **Expected (plaintext):** Receiver may emit a plaintext error frame (`0x00` + "Bad indicator byte") then close.
- **Noise steps:** Send a frame starting with `0x00` during Noise.
- **Expected (Noise):** Treated as bad indicator → close / reject.

### TS-12.4 (detail)

- **Steps:** Send a header declaring a payload length far larger than actual.
- **Expected:** Implementation bounds the allocation, waits for data or times out; **no** unbounded memory growth / crash.

### TS-12.1 (detail)

- **Steps:** Send a valid frame with type = `9999` (undefined).
- **Expected:** Receiver ignores it; subsequent valid messages still processed.

---

## TS-13: Interoperability

| ID | Role | Level | Description |
|---|---|---|---|
| TS-13.1 | CLIENT | MUST | Connects to a real ESPHome device (Profile A, Noise) |
| TS-13.2 | CLIENT | MUST | Connects to a real ESPHome device (Profile B, plaintext) |
| TS-13.3 | SERVER | MUST | Accepts connection from Home Assistant |
| TS-13.4 | SERVER | MUST | Accepts connection from `esphome` CLI (`esphome logs`) |
| TS-13.5 | CLIENT | SHOULD | Full discovery + state + command against kmeter device |
| TS-13.6 | BOTH | SHOULD | Survives 24 h soak test with periodic state changes |
| TS-13.7 | CLIENT | SHOULD | Reconnects automatically after device reboot |

### TS-13.5 (detail) — end-to-end acceptance

1. Connect (Noise) to `kmeter`.
2. Authenticate.
3. Discover all 4 entities (3 sensors + 1 button).
4. Subscribe states; receive temperature readings.
5. Press `restart_kmeter` button (type 62).
6. Verify device reboots and client reconnects.

---

## TS-14: Logging

The native API can stream device logs to subscribed clients
(`SubscribeLogsRequest` 28 → `SubscribeLogsResponse` 29).

| ID | Role | Level | Description |
|---|---|---|---|
| TS-14.1 | CLIENT | SHOULD | Sends SubscribeLogsRequest (28) with a minimum `level` |
| TS-14.2 | SERVER | SHOULD | Streams SubscribeLogsResponse (29) at/above the requested level |
| TS-14.3 | SERVER | SHOULD | `LogLevel` values follow `NONE=0` … `VERY_VERBOSE=7` |
| TS-14.4 | SERVER | MAY | Sets `send_failed = true` when a line cannot be buffered |

### TS-14.2 (detail)

- **Steps:** Subscribe at `DEBUG` (5); trigger a device log line.
- **Expected:** Client receives a `SubscribeLogsResponse` whose `level` ≤ the
  requested minimum and whose `message` matches the emitted line.
- **Evidence:** Decoded log line (the test bed asserts this as `TS-10-LOG`).

> **Implementation status:** implemented on the device side and exercised by the
> test bed's `TS-10-LOG` case (also fed by the Serial → `api.log()` bridge).

---

## TS-15: Home Assistant Pairing & Discovery

These cases cover regressions found while pairing Bentuino firmware with Home
Assistant: blank encryption keys, missing mDNS hints, plaintext rejection on
Noise-only devices, passwordless Hello-only discovery, and incorrect
`DeviceInfoResponse` field numbers.

| ID | Role | Level | Description |
|---|---|---|---|
| TS-15.1 | SERVER | MUST | Passwordless device: Hello → ListEntities **without** ConnectRequest succeeds |
| TS-15.2 | SERVER | MUST | Noise-required device: plaintext TCP client receives reject byte `0x01` then close |
| TS-15.3 | SERVER | MUST | `DeviceInfoResponse.manufacturer` encoded on protobuf field **12**, not field 8 |
| TS-15.4 | SERVER | SHOULD | mDNS advertises `_esphomelib._tcp` on the API port with standard TXT keys |
| TS-15.5 | SERVER | SHOULD | mDNS TXT `api_encryption` present when Noise is required; absent when plaintext |

### TS-15.1 (detail) — Hello-only entity discovery

- **Background:** Home Assistant may treat passwordless API devices as
  authenticated immediately after `HelloResponse` and send `ListEntitiesRequest`
  without `ConnectRequest`. Servers that still require Connect leave HA with a
  paired device but **zero entities**.
- **Precond:** Device `api.password` is empty (plaintext or Noise transport).
- **Steps:**
  1. Complete transport handshake (plaintext `0x00` or Noise as configured).
  2. Send `HelloRequest` (1); receive `HelloResponse` (2).
  3. **Do not** send `ConnectRequest` (3).
  4. Send `ListEntitiesRequest` (11).
- **Expected:** Server streams `ListEntities*Response` messages and finishes with
  `ListEntitiesDoneResponse` (19). Connection stays open.
- **Fail case:** TCP close, protocol error, or timeout waiting for entity list.
- **Evidence:** Decoded entity count ≥ 1 (or matches device entity manifest).
- **Automated:** `TS-15.1` in the Python test bed (skipped when a password is
  configured).

### TS-15.2 (detail) — RequiresEncryption reject frame

- **Background:** When encryption is enabled but HA discovers the device without
  `api_encryption` in mDNS (or the user adds by IP), the client may attempt a
  plaintext connection. Without an explicit reject, HA shows a generic
  “includes an `api` section” error instead of prompting for the key.
- **Precond:** Server Profile A (Noise required; non-empty `api.encryption.key`).
- **Steps:**
  1. Open TCP to the API port.
  2. Send plaintext transport indicator `0x00` (do **not** start Noise).
- **Expected:** Server writes a single byte `0x01` (`RequiresEncryptionAPIError`
  in aioesphomeapi) then closes the socket.
- **Evidence:** Capture showing `0x01` as the first received byte before FIN/RST.
- **Automated:** `TS-15.2` (skipped on plaintext-only devices).

### TS-15.3 (detail) — DeviceInfo manufacturer field

- **Background:** In API proto 1.10, field **8** is `project_name` and field
  **12** is `manufacturer`. Encoding manufacturer on field 8 mislabels the
  device in Home Assistant and breaks field semantics.
- **Precond:** Authenticated session (Hello + Connect, or Hello-only on
  passwordless per TS-15.1).
- **Steps:** Send `DeviceInfoRequest` (9); decode `DeviceInfoResponse` (10).
- **Expected:** Any manufacturer string appears on field **12**. Field **8**
  (`project_name`) must not be the sole carrier of the manufacturer value.
- **Optional check:** When `expected_manufacturer` is configured in the test bed,
  field 12 must equal that value.
- **Automated:** `TS-15.3`.

### TS-15.4 (detail) — mDNS service advertisement

- **Background:** Bentuino firmware does not advertise `_esphomelib._tcp` by
  default; without it, HA cannot auto-discover the device on the LAN.
- **Precond:** Device on the same network as the tester; mDNS hostname known
  (ESPHome node name, e.g. `thermal-solar-supervisor-api`).
- **Steps:** Resolve `_esphomelib._tcp.local.` for
  `<name>._esphomelib._tcp.local.`
- **Expected:**
  - Service type `_esphomelib._tcp`
  - Port matches API port (default 6053)
  - TXT includes at least `version`, `address`, `mac`, `friendly_name` (ESPHome
    convention)
- **Automated:** `TS-15.4` when `--mdns-name` / config `mdns_name` is set
  (requires `zeroconf`).

### TS-15.5 (detail) — mDNS `api_encryption` TXT

- **Background:** Home Assistant reads `api_encryption` from mDNS to decide
  whether to prompt for the Noise PSK during setup.
- **Precond:** Same as TS-15.4.
- **Steps:** Inspect TXT records on the `_esphomelib._tcp` instance.
- **Expected:**
  - **Noise enabled:** `api_encryption=Noise_NNpsk0_25519_ChaChaPoly_SHA256`
  - **Plaintext:** key **absent** (do not advertise encryption when none is used)
- **Automated:** `TS-15.5` when `--mdns-name` is set.

---

## 19. Conformance Profiles

An implementation declares which profile(s) it claims.

| Profile | Required test suites |
|---|---|
| **Minimal Client (read-only)** | TS-1 or TS-2, TS-3, TS-4 (client), TS-5, TS-6, TS-7, TS-9, TS-11, TS-12, TS-13.1/2 |
| **Full Client** | All of Minimal + TS-8, TS-10, TS-13 |
| **Minimal Server** | TS-1 or TS-2, TS-3, TS-4, TS-5, TS-6, TS-7, TS-9, TS-11, TS-12 |
| **Full Server** | All of Minimal Server + TS-8, TS-10, TS-13.3/4 |
| **HA-facing Server** | Full Server + TS-15 (all MUST; document SHOULD mDNS deviations) |
| **Encrypted profile** | Adds full TS-2 (mandatory) + TS-15.2 MUST |
| **Plaintext profile** | Adds full TS-1 (mandatory) |

A claim of conformance MUST pass all **MUST** tests in the chosen profile's suites and document any **SHOULD** deviations.

---

## 20. Results Matrix Template

Copy per test run.

```
Implementation:  ____________________   Role: CLIENT / SERVER
Profile claimed: ____________________   Transport: Noise / Plaintext
API version:     1.10                   Date: __________  Tester: __________
Counterpart:     ____________________   (device/client used)
```

| Test ID | Level | Result (P/F/N/A) | Evidence ref | Notes |
|---|---|---|---|---|
| TS-1.1 | MUST | | | |
| TS-1.2 | MUST | | | |
| TS-1.3 | MUST | | | |
| TS-1.4 | MUST | | | |
| TS-1.5 | MUST | | | |
| TS-1.6 | SHOULD | | | |
| TS-1.7 | MUST | | | |
| TS-1.8 | MUST | | | |
| TS-2.1 | MUST | | | |
| TS-2.2 | MUST | | | |
| TS-2.3 | MUST | | | |
| TS-2.4 | MUST | | | |
| TS-2.5 | MUST | | | |
| TS-2.6 | MUST | | | |
| TS-2.7 | MUST | | | |
| TS-2.8 | MUST | | | |
| TS-2.9 | MUST | | | |
| TS-2.10 | MUST | | | |
| TS-2.11 | SHOULD | | | |
| TS-2.12 | MUST | | | |
| TS-2.13 | MUST | | | |
| TS-3.1 … TS-3.9 | … | | | |
| TS-4.1 … TS-4.9 | … | | | |
| TS-5.1 … TS-5.6 | … | | | |
| TS-6.1 … TS-6.9 | … | | | |
| TS-7.1 … TS-7.9 | … | | | |
| TS-8.1 … TS-8.7 | … | | | |
| TS-9.1 … TS-9.8 | … | | | |
| TS-10.1 … TS-10.6 | … | | | |
| TS-11.1 … TS-11.6 | … | | | |
| TS-12.1 … TS-12.10 | … | | | |
| TS-13.1 … TS-13.7 | … | | | |
| TS-14.1 … TS-14.4 | … | | | |
| TS-15.1 … TS-15.5 | … | | | |

Legend: P = Pass, F = Fail, N/A = Not Applicable to role/profile.
```

### Summary

```
MUST   tests passed: ____ / ____
SHOULD tests passed: ____ / ____
Deviations documented:
  - ____________________________________________
Overall: CONFORMANT / NON-CONFORMANT / CONFORMANT-WITH-DEVIATIONS
```

---

## License & Attribution

Derived from the [ESPHome](https://esphome.io) project (AGPL-3.0). Test plan for Bentuino interoperability validation.
