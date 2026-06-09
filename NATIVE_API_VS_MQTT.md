# ESPHome Native API vs MQTT — Comparison Guide

This document compares the **Bentuino ESPHome Native API** implementation
(`firmware/lib/ESPHomeAPI`, API 1.10) with a typical **MQTT-based** approach for
connecting embedded devices to Home Assistant. It is written for firmware authors
deciding how to expose a custom ESP32/ESP8266 device to HA, and for anyone
evaluating whether to adopt this library or roll their own MQTT layer.

> **Upstream comparison:** ESPHome's official docs include an
> [Advantages over MQTT](https://esphome.io/components/api/#advantages-over-mqtt)
> section. They note that *"MQTT is a great protocol and will never be removed"*
> while listing efficiency, one-click HA setup, and broker-independence as Native
> API strengths. This document extends that discussion for the Bentuino C++
> library.

**Scope:** Home Assistant as the primary consumer on a local LAN. Cloud MQTT
brokers, AWS IoT, and vendor-specific stacks are mentioned only where they
change the trade-offs.

---

## Table of Contents

1. [Executive summary](#1-executive-summary)
2. [Architecture at a glance](#2-architecture-at-a-glance)
3. [Similarities](#3-similarities)
4. [Key differences](#4-key-differences)
5. [Feature comparison matrix](#5-feature-comparison-matrix)
6. [Pros and cons](#6-pros-and-cons)
7. [When to choose which](#7-when-to-choose-which)
8. [Hybrid and migration paths](#8-hybrid-and-migration-paths)
9. [References](#9-references)

---

## 1. Executive summary

Both approaches solve the same problem: **make a microcontroller look like a
first-class Home Assistant device** — discoverable entities, live state, remote
commands, automations.

| | **ESPHome Native API** (this project) | **MQTT** (typical HA setup) |
|---|---|---|
| **Pattern** | Device runs a TCP **server**; HA connects as **client** | Device is MQTT **client**; HA and device share a **broker** |
| **Wire format** | Protobuf over a custom framing layer | UTF-8 payloads on named **topics** |
| **Discovery** | `ListEntities*` messages in a single session | HA **MQTT Discovery** (JSON config topics) |
| **Coupling to HA** | Uses the built-in **ESPHome** integration | Uses the **MQTT** integration |
| **Extra infrastructure** | None (point-to-point TCP) | Requires an MQTT **broker** (e.g. Mosquitto, HA add-on) |

Neither is universally “better.” The Native API optimizes for **tight HA
integration, rich entity semantics, and zero broker dependency**. MQTT optimizes
for **ecosystem breadth, multi-subscriber pub/sub, and decoupling through a
message bus**.

---

## 2. Architecture at a glance

### ESPHome Native API (this implementation)

```
┌──────────────┐         TCP :6053          ┌─────────────────────┐
│ Home         │  ───────────────────────►  │ ESP32 / ESP8266     │
│ Assistant    │  ◄───────────────────────  │ APIServer (device)  │
│ (ESPHome     │   Protobuf + Noise opt.    │ entities, commands  │
│  integration)│                            └─────────────────────┘
└──────────────┘
```

- The **device listens**; HA initiates the connection.
- One (or a few) long-lived TCP sessions per device.
- Entity list, state subscription, and commands are **typed messages** with
  numeric IDs (see `api.proto` / `api_message_types.h`).
- Optional **Noise** transport encryption (`Noise_NNpsk0_25519_ChaChaPoly_SHA256`)
  plus optional application **password** at the Connect step.

### MQTT (typical Home Assistant pattern)

```
┌──────────────┐                            ┌─────────────────────┐
│ Home         │◄──── subscribe/publish ───►│ MQTT broker         │
│ Assistant    │                            │ (Mosquitto, etc.)   │
│ (MQTT        │                            └──────────▲──────────┘
│  integration)│                                       │
└──────────────┘                            publish/subscribe
                                            ┌──────────┴──────────┐
                                            │ ESP32 / ESP8266       │
                                            │ MQTT client           │
                                            └───────────────────────┘
```

- The **broker is the hub**; device and HA never talk directly.
- Many clients can subscribe to the same topic.
- Discovery: device publishes JSON to
  `homeassistant/<component>/<node>/<object_id>/config`.
- State: device publishes to `…/state` (or a custom topic); HA subscribes.
- Commands: HA publishes to `…/command`; device subscribes.

---

## 3. Similarities

Despite different transports, both stacks converge on the same user-visible
outcome in Home Assistant:

| Concern | Native API | MQTT |
|---|---|---|
| **Entity types** | sensor, switch, light, climate, … (22 in this lib) | Same HA platforms via discovery `component` field |
| **Live state** | Server **pushes** `*StateResponse` after subscribe | Device **publishes** on state topics |
| **Remote control** | HA sends `*CommandRequest` by entity **key** | HA **publishes** command payload to command topic |
| **Automations** | Standard HA entity triggers/actions | Standard HA entity triggers/actions |
| **Local LAN** | Both are commonly deployed on a private network | Same |
| **Security expectation** | Encryption + password supported (Noise + Connect password) | TLS to broker + username/password or certs common |
| **Offline behaviour** | HA marks device unavailable when TCP session drops | HA marks entities unavailable when MQTT LWT / last message timing says so |

From an automation author's perspective, **once entities exist in HA, both look
the same** — `switch.turn_on`, `climate.set_temperature`, etc.

---

## 4. Key differences

### 4.1 Connection model

**Native API:** Session-oriented. HA opens TCP, completes Hello → Connect →
ListEntities → SubscribeStates, then multiplexes commands, state updates, pings,
and optional log streaming on one connection.

**MQTT:** Connectionless at the application level. Each message is independent;
session state is emulated by retained messages, last-will, and client
re-subscriptions.

**Implication:** The Native API gives you a single place to enforce auth, rate
limits, and capability negotiation per connection. MQTT pushes that to the broker
(ACLs) and to topic design conventions.

### 4.2 Discovery and entity identity

**Native API:**

- Discovery is a **structured binary protocol**: one `ListEntities*Response` per
  entity, terminated by `ListEntitiesDoneResponse`.
- Runtime identity is a **fixed32 FNV-1 hash** of `object_id` (not the string
  itself) in all state/command messages.
- Metadata (name, unit, min/max, color modes, …) travels in list messages with
  typed fields.

**MQTT:**

- Discovery is **JSON documents** on well-known config topics, usually once at
  boot (and again after reconnect if you re-publish).
- Identity is the **topic tree** + `unique_id` in the discovery payload.
- Metadata is whatever the JSON schema for that component supports; typos or
  schema drift show up as mis-configured entities.

**Implication:** Native API discovery is heavier to implement but **self-describing
and versioned** (API major/minor). MQTT discovery is human-readable in
`mosquitto_sub` but you must keep JSON in sync with HA's evolving discovery spec.

### 4.3 Payload encoding

**Native API:** Protocol Buffers (proto3). Optional fields use explicit `has_*`
semantics — partial light/cover/climate commands are first-class.

**MQTT:** Almost always plain text (`"ON"`/`"OFF"`, `"23.5"`) or JSON. Partial
updates depend on convention; many setups use separate topics per attribute.

**Implication:** Native API commands like “set brightness only, leave color
unchanged” map naturally to the wire format. MQTT often needs topic-per-attribute
or a JSON schema you maintain yourself.

### 4.4 Infrastructure dependencies

**Native API:** No broker. Firewall must allow HA → device:6053 (or your chosen
port). Each HA instance (or ESPHome CLI client) connects directly.

**MQTT:** Broker must be running, reachable by **both** device and HA, and
sized for your fan-out. Broker downtime affects **all** MQTT devices.

**Implication:** Native API is simpler on a **single-HA, few-device** LAN. MQTT
scales better when many services (Node-RED, multiple HA instances, logging
pipelines) consume the same telemetry.

### 4.5 Multi-consumer / fan-out

**Native API:** Designed for HA (and ESPHome-compatible clients). Third parties
need to speak the Native API protocol (see `IMPLEMENTATION_GUIDE.md`).

**MQTT:** Any subscriber with broker access can read/write topics — analytics,
 Grafana, another HA, mobile apps, cloud bridges.

**Implication:** MQTT wins when the device data is an **event stream for many
systems**. Native API wins when HA is the **sole control plane** and you want
minimum moving parts.

### 4.6 Logs, time sync, and HA callbacks

This implementation includes features that **have no MQTT equivalent** in the
standard HA MQTT discovery flow:

| Feature | Native API (implemented) | Typical MQTT |
|---|---|---|
| **Device log streaming** | `SubscribeLogsRequest` / `SubscribeLogsResponse`; Serial bridge | Not standard; custom topic + HA doesn't ingest natively |
| **Time sync from HA** | `GetTimeRequest` / `GetTimeResponse` | NTP on device, or custom topic |
| **Device → HA service calls** | `call_homeassistant_service()`, `fire_homeassistant_event()` | Possible via HA MQTT **trigger** topics (device publishes → automation), but not entity-native |
| **Device ← HA state** | `subscribe_homeassistant_state("sun.sun", …)` | Subscribe to HA state export (unusual) or duplicate logic on device |
| **Camera snapshot stream** | `CameraImageRequest` + chunked image responses | Separate HTTP/camera stack or custom MQTT binary (awkward) |

These are differentiators for **ESPHome-parity** firmware, not for a minimal
sensor node.

### 4.7 Security models

**Native API (this lib):**

- Optional **Noise PSK** (32-byte key, base64 in config) — encrypted transport.
- Optional **application password** in `ConnectRequest`.
- Plaintext mode available (not recommended on untrusted networks).
- No built-in rate limiting on auth failures (see `REFERENCE.md` open items).

**MQTT:**

- **TLS** to broker is the common production pattern.
- Broker **ACLs** per topic (fine-grained if you invest in topic layout).
- Credentials often shared across many devices unless you issue per-device certs.
- Retained messages can **leak last state** to any subscriber with topic access.

**Implication:** Both are adequate on a trusted LAN with encryption enabled.
MQTT's security story is **broker-centric**; Native API's is **per-device
session-centric**.

### 4.8 Firmware footprint and complexity

Reference build sizes from `firmware/README.md` (demo + Noise, ESP32):

| | Native API (this lib) | MQTT (typical) |
|---|---|---|
| **Flash** | ~46% of 1.3 MB (~621 KB) | ~200–400 KB depending on stack (PubSubClient + TLS + JSON), often less without TLS |
| **RAM** | ~12% (~40 KB) | Often lower for minimal pub/sub, higher with TLS + large payloads |
| **Code you maintain** | Entity classes + hardware logic | Topic layout + JSON discovery + command parsing + reconnect logic |

The Native API library carries **protobuf codec, framing, Noise crypto, and
full entity encoders** — heavier than a minimal MQTT sensor, lighter than
implementing equivalent semantics correctly yourself on MQTT.

### 4.9 Reconnect and availability

**Native API:** HA's ESPHome integration handles reconnect, encryption, and
entity re-registration. Device-side: accept TCP, serve session.

**MQTT:** Device must implement **exponential backoff**, resubscribe, and
**re-publish discovery** after broker outages. HA uses **availability topics**
and **birth/will** messages for entity state.

Both require thought; MQTT pushes more **reconnect policy** into firmware.

### 4.10 Ecosystem and tooling

**Native API:**

- First-class in HA via **ESPHome integration** (no MQTT integration config).
- `esphome logs` speaks the same protocol.
- Third-party Python clients can use **[aioesphomeapi](https://github.com/esphome/aioesphomeapi)**
  without running HA (see [community discussion](https://community.home-assistant.io/t/integrating-with-esphome-sensor/550879)).
- This repo includes a **compliance test bed** (35 MUST tests over Noise).

**MQTT:**

- Universal across HA, Node-RED, openHAB, cloud vendors.
- HA lists [dozens of tools with built-in MQTT discovery](https://www.home-assistant.io/integrations/mqtt/#support-by-third-party-tools).
- Huge library ecosystem (`PubSubClient`, `esp-mqtt`, ArduinoMqttClient, …).
- Debug with any MQTT client; no protobuf schema required.

### 4.11 Battery / deep sleep (important edge case)

The **device-as-server** model is a known limitation for intermittently connected
nodes. Upstream ESPHome and the HA community consistently recommend **MQTT over
Native API** for deep-sleep battery devices:

- ESPHome maintainers: deep sleep is *"not really compatible with native API"*
  because HA polls for connectability (~60 s backoff) while the device must wait
  for HA to connect ([esphome/issues#350](https://github.com/esphome/issues/issues/350)).
- A long-running feature request proposes a future **Native API client mode**
  with a central server in HA, analogous to a broker
  ([esphome/feature-requests#46](https://github.com/esphome/feature-requests/issues/46)).
- Community benchmarks show missed wake events with API vs reliable MQTT publish
  on wake ([HA community — API vs MQTT](https://community.home-assistant.io/t/api-vs-mqtt/133737),
  [deep sleep comparison 2022](https://community.home-assistant.io/t/esphome-api-vs-mqtt-for-deep-sleep-in-2022/440934)).

Recent ESPHome releases improved graceful disconnect before sleep
([esphome#9008](https://github.com/esphome/esphome/pull/9008)), but the
fundamental **push vs poll** topology remains.

**Implication:** Mains-powered LAN devices (this repo's primary target) suit
Native API well. Battery door/window sensors should stay on MQTT (or ESP-NOW +
gateway) until client-mode API exists.

---

## 5. Feature comparison matrix

Legend: ✅ supported well · ⚠ partial / convention-dependent · ❌ not standard · — not applicable

| Capability | Bentuino Native API (v0.4.1) | MQTT + HA discovery |
|---|:---:|:---:|
| Sensor / binary_sensor / switch / light / … (22 types) | ✅ | ✅ |
| Automatic HA discovery | ✅ (ListEntities) | ✅ (JSON config topics) |
| Push state on change | ✅ | ✅ (publish) |
| Commands with typed optional fields | ✅ (protobuf) | ⚠ (string/JSON convention) |
| Button (stateless) | ✅ | ✅ |
| Event entity | ✅ | ⚠ (often workarounds) |
| Camera snapshot API | ✅ (hooks; JPEG is app code) | ❌ / custom |
| Log streaming to HA | ✅ | ❌ |
| Time sync from HA | ✅ | ❌ |
| Device calls HA services / events | ✅ | ⚠ (MQTT trigger automations) |
| Device subscribes to HA entity state | ✅ | ❌ |
| Switch `restore_mode` persistence | ✅ (NVS) | ⚠ (retain + local NVS yourself) |
| Noise / encrypted transport | ✅ | ⚠ (TLS to broker, separate concern) |
| No middlebox broker | ✅ | ❌ |
| Multiple independent subscribers | ❌ | ✅ |
| Works without Home Assistant | ⚠ (needs Native API client) | ✅ |
| Human-readable wire format | ❌ (binary) | ✅ |
| Standardized compliance tests in this repo | ✅ | — |
| User-defined device services | ❌ (not yet) | ⚠ (custom topics) |
| Bluetooth proxy / Voice Assistant | ❌ | ❌ |

---

## 6. Pros and cons

### ESPHome Native API (this implementation)

**Pros**

- **Zero broker** — one less service to run, patch, and back up.
- **Native HA integration** — add device by IP; entities appear with full
  semantics (climate modes, cover position, light color modes, etc.).
- **Rich protocol** — logs, time sync, HA service calls, HA state subscription,
  camera hooks built into the session.
- **Typed commands** — partial updates and complex entities match ESPHome
  behaviour without inventing JSON schemas.
- **Validated stack** — compliance test bed, host simulator, and entity
  examples reduce integration risk.
- **Encryption aligned with ESPHome** — same Noise PSK model as YAML
  `api.encryption.key`.

**Cons**

- **HA-centric** — third-party consumers must implement the Native API (or you
  bridge elsewhere).
- **Device listens on TCP** — less natural behind strict NAT or when the device
  cannot accept inbound connections; HA must reach the device (fine on LAN, awkward
  for remote-only devices).
- **Heavier firmware** — protobuf + Noise + entity layer vs a thin MQTT publish.
- **Opaque on the wire** — debugging needs `esphome logs`, the test bed, or
  packet captures + schema knowledge.
- **Incomplete ESPHome parity** — no user-defined services, BLE proxy, or voice
  assistant (see `firmware/REFERENCE.md`).
- **Plaintext mode risk** — if `encryption_key` is empty, traffic is unencrypted
  (same class of mistake as MQTT without TLS).

### MQTT

**Pros**

- **Universal pub/sub** — many subscribers, integrations, and cloud bridges without
  changing firmware.
- **Broker as control point** — ACLs, bridges, persistence, and monitoring in
  one place.
- **Simple payloads** — `"23.5"`, `"ON"`, easy to inspect with `mosquitto_sub`.
- **Inbound-friendly device** — device only connects outward to broker; works
  when inbound TCP to the device is blocked (still needs broker reachability).
- **Mature tooling** — decades of MQTT experience, countless libraries.
- **Retained messages** — new subscribers instantly see last known state (also a
  con for privacy).

**Cons**

- **Broker dependency** — single point of failure unless clustered.
- **Topic + discovery discipline** — you design and document the topic tree;
  mistakes cause duplicate entities or silent failures.
- **Weaker entity semantics on the wire** — complex types (climate, cover, light
  color) need careful JSON or many topics; easy to diverge from HA expectations.
- **No standard logs / HA callback channel** — features this Native API lib
  provides require custom topics and HA-side glue.
- **Reconnect logic in firmware** — rediscovery, resubscribe, LWT, availability
  templates add code and test surface.
- **Security complexity with TLS** — cert management on ESP8266 is tight;
  shared broker credentials weaken per-device isolation.

---

## 7. When to choose which

### Prefer **ESPHome Native API** (this library) when:

- Home Assistant is the **primary or only** home automation hub.
- You want **ESPHome-like behaviour** (logs in HA, encryption, rich entities)
  without adopting the full ESPHome build system.
- You control the LAN and HA can **reach the device on TCP 6053**.
- You want **one protocol** from discovery through camera/events, validated by
  the bundled test bed.
- You are building **Bentuino / custom C++ firmware** already using this repo.

### Prefer **MQTT** when:

- You already run **Mosquitto** (or HA's MQTT add-on) for other devices.
- **Multiple consumers** need the same telemetry (HA + Node-RED + recorder).
- The device **cannot accept inbound TCP** but can outbound-connect to a broker.
- You need **cloud or multi-site** fan-out through a broker bridge.
- Team familiarity with **topics and JSON** exceeds appetite for protobuf +
  Noise handshake implementation.
- Firmware budget is **extremely tight** and you only need a few simple sensors.

### Either works when:

- Local network, single HA, simple sensors + switches, trusted LAN.
- You wrap hardware logic cleanly — only the **transport layer** differs.

---

## 8. Hybrid and migration paths

You are not locked in forever:

| Approach | Idea |
|---|---|
| **Native API only** | Simplest for HA-only; matches this repo's examples. |
| **MQTT only** | Use HA MQTT discovery; ignore Native API entirely. |
| **Dual stack** | Rare; only if you must feed a broker *and* HA Native API — doubles flash, RAM, and test burden. Avoid unless required. |
| **Bridge** | Device speaks Native API; a small service on HA publishes to MQTT for other tools — keeps firmware simple, adds server-side glue. |
| **ESPHome YAML** | Upstream ESPHome can expose **both** Native API and MQTT from one config — enable `api` + `mqtt` with `discovery: false` and `discover_ip: true` so HA finds the device via MQTT but uses the richer Native API session ([ESPHome MQTT docs](https://esphome.io/components/mqtt/)). This C++ library does not include an MQTT component. |

Migrating **from MQTT to Native API:** redefine entities using `entity_types.h`,
map old topics to `object_id`s, re-pair in HA via ESPHome integration instead of
MQTT discovery.

Migrating **from Native API to MQTT:** export entity metadata as discovery JSON,
replace `publish_state()` with MQTT publishes, replace command handlers with
MQTT subscribe callbacks — effectively a rewrite of the transport layer.

---

## 9. References

### This repository

| Resource | URL |
|---|---|
| Native API protocol overview | [README.md](README.md) |
| Client implementation guide | [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md) |
| C++ library API & limitations | [firmware/REFERENCE.md](firmware/REFERENCE.md) |
| Per-entity examples (22 types) | [firmware/examples/README.md](firmware/examples/README.md) |
| Compliance testing | [TEST_STRATEGY.md](TEST_STRATEGY.md), [testbed/README.md](testbed/README.md) |
| Canonical message schema | [api.proto](api.proto) |

### Official ESPHome documentation

| Topic | URL |
|---|---|
| **Native API component** (config, encryption, *Advantages over MQTT*) | [esphome.io/components/api/](https://esphome.io/components/api/) |
| **MQTT client component** (discovery, `discover_ip`, hybrid with API) | [esphome.io/components/mqtt/](https://esphome.io/components/mqtt/) |
| **Protocol details** (Noise vs plaintext framing, wire formats) | [developers.esphome.io — Protocol Details](https://developers.esphome.io/architecture/api/protocol_details/) |
| **API encryption required** (password auth removed, 2026.1.0) | [ESPHome 2026.1.0 changelog](https://esphome.io/changelog/2026.1.0/) |
| **Deep sleep component** | [esphome.io/components/deep_sleep/](https://esphome.io/components/deep_sleep/) |

### Home Assistant documentation

| Topic | URL |
|---|---|
| **ESPHome integration** (Native API, events, HA entity state retrieval) | [home-assistant.io/integrations/esphome/](https://www.home-assistant.io/integrations/esphome/) |
| **MQTT integration** | [home-assistant.io/integrations/mqtt/](https://www.home-assistant.io/integrations/mqtt/) |
| **MQTT Discovery** (JSON config topics, `unique_id`, third-party tools) | [MQTT Discovery section](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery) |

### Protocol, security, and client libraries

| Topic | URL |
|---|---|
| **Noise Protocol Framework** (used instead of TLS for Native API encryption) | [noiseprotocol.org](https://noiseprotocol.org/noise.html) |
| **Why Noise over TLS/SSL** for Native API (size/speed vs MQTT TLS) | [esphome/feature-requests#133](https://github.com/esphome/feature-requests/issues/133) |
| **aioesphomeapi** — Python Native API client (usable without HA) | [github.com/esphome/aioesphomeapi](https://github.com/esphome/aioesphomeapi) |
| Reference Node.js client | [github.com/hjdhjd/esphome-client](https://github.com/hjdhjd/esphome-client) |

### GitHub issues and design discussions

| Topic | URL |
|---|---|
| **Native API client/server mode** (future HA-hosted server, deep sleep) | [esphome/feature-requests#46](https://github.com/esphome/feature-requests/issues/46) |
| **Deep sleep + Native API connection failures** | [esphome/issues#350](https://github.com/esphome/issues/issues/350) |
| **Graceful API teardown before deep sleep** | [esphome/esphome#9008](https://github.com/esphome/esphome/pull/9008) |
| **MQTT discovery unique_id collisions** (use `discovery_unique_id_generator: mac`) | [esphome/issues#4840](https://github.com/esphome/issues/issues/4840) |

### Home Assistant community threads

| Topic | URL |
|---|---|
| **API vs MQTT** — missed binary-sensor states on wake (MQTT faster) | [community.home-assistant.io/t/api-vs-mqtt/133737](https://community.home-assistant.io/t/api-vs-mqtt/133737) |
| **API vs MQTT for deep sleep** (2022 benchmarks, HA polling latency) | [community.home-assistant.io/t/esphome-api-vs-mqtt-for-deep-sleep-in-2022/440934](https://community.home-assistant.io/t/esphome-api-vs-mqtt-for-deep-sleep-in-2022/440934) |
| **Using aioesphomeapi without HA or MQTT** | [community.home-assistant.io/t/integrating-with-esphome-sensor/550879](https://community.home-assistant.io/t/integrating-with-esphome-sensor/550879) |
| **MQTT discovery duplicate entities** (`discovery_unique_id_generator: mac`) | [community.home-assistant.io/t/mqtt-discovery-only-fully-detecting-one-esphome/617361](https://community.home-assistant.io/t/mqtt-discovery-only-fully-detecting-one-esphome/617361) |

---

*Document version: 2026-06-09 — reflects Bentuino ESPHomeAPI library v0.4.1.*
