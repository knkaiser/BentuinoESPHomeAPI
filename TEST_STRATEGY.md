# Test Strategy & Architecture

How the ESPHome native API implementation in this repository is verified: the
testing layers, the components involved, how they interconnect, and how to run
each one.

Related documents:
- [`COMPLIANCE_TEST_PLAN.md`](COMPLIANCE_TEST_PLAN.md) — the catalog of `TS-*`
  test cases and conformance profiles (the *what*).
- [`testbed/README.md`](testbed/README.md) — the Python test bed (the *runner*).
- [`firmware/README.md`](firmware/README.md) — the C++ device implementation
  under test.
- [`firmware/REFERENCE.md`](firmware/REFERENCE.md) — library API + open items.

---

## 1. Goals

1. **Specification fidelity** — prove the wire format (framing, protobuf
   encoding, FNV-1 keys, lifecycle) matches the protocol documented in
   [`README.md`](README.md) / [`IMPLEMENTATION_GUIDE.md`](IMPLEMENTATION_GUIDE.md).
2. **Two-sided validation** — exercise both a **client** implementation (Python
   test bed, acting like Home Assistant) and a **server** implementation (the
   C++ firmware, acting like an ESPHome device).
3. **Run without hardware** — the entire suite must be runnable on a developer
   laptop / CI with no ESP board attached, while remaining valid on real
   hardware.
4. **CI-friendly** — a single command returns a pass/fail matrix and a non-zero
   exit code on any `MUST` failure.

---

## 2. The components

| Component | Role | Language | Location |
|---|---|---|---|
| **Test bed** | Protocol **client** (Home Assistant side) + test runner | Python 3 | `testbed/` |
| **Firmware library** | Protocol **server** (device side) | C++17 | `firmware/lib/ESPHomeAPI/` |
| **Host simulator** | Runs the *real* firmware code over POSIX sockets | C++17 + shim | `firmware/test/host_sim/` |
| **Firmware host test** | Unit test of the firmware's portable core | C++17 | `firmware/test/host_test.cpp` |
| **Compliance plan** | Source-of-truth test definitions | Markdown | `COMPLIANCE_TEST_PLAN.md` |

Both the Python client and the C++ server contain an **independent
implementation** of the protocol's framing and protobuf codec. They are
cross-checked against the **same wire vectors** (e.g. `HelloRequest` =
`0a08…1001180a`, key `0xDB0A35EC` → `0dec350adb`, float `23.5` → `150000bc41`).
A bug in one is unlikely to be mirrored in the other, so agreement is strong
evidence of correctness.

---

## 3. Testing layers

The strategy uses four layers, increasing in integration scope:

```
┌─────────────────────────────────────────────────────────────────────┐
│ L4  Home Assistant interop (manual)                                   │
│     Real HA  ⇄  device (real ESP32 or host simulator)                 │
├─────────────────────────────────────────────────────────────────────┤
│ L3  End-to-end integration (automated, no hardware)                   │
│     Python test bed  ⇄  TCP  ⇄  host simulator (REAL firmware code)   │
├─────────────────────────────────────────────────────────────────────┤
│ L2  Component unit tests (automated, no hardware)                     │
│     • testbed offline tests (Python codec / framing / FNV-1)          │
│     • firmware host_test (codec / FNV-1 / entities / restore_mode)    │
├─────────────────────────────────────────────────────────────────────┤
│ L1  Build verification                                                │
│     pio run -e esp32dev / esp32-c3 / nodemcuv2   (compiles for target)│
└─────────────────────────────────────────────────────────────────────┘
```

### L1 — Build verification

Confirms the firmware compiles and links for each supported target and fits in
flash/RAM.

```bash
cd firmware
pio run -e esp32dev -e esp32-c3 -e nodemcuv2
```

### L2 — Component unit tests (offline)

Pure wire-format / algorithm tests, no sockets. These validate the two codecs
independently against the shared vectors.

Python side (test bed, `offline` mode tests):

```bash
cd testbed
python run_tests.py --offline-only
```

C++ side (firmware portable core):

```bash
cd firmware/test
c++ -std=c++17 -I ../lib/ESPHomeAPI/src host_test.cpp -o host_test && ./host_test
```

### L3 — End-to-end integration (the key layer)

The Python test bed drives the **actual firmware code** over a real TCP socket.
Because we have no ESP board in CI, the firmware's `APIServer` /
`APIConnection` / `Entity` sources are compiled into a **host simulator** using
a thin POSIX shim for the Arduino `WiFiServer` / `WiFiClient` / `millis()`. No
protocol logic is reimplemented for the host — only the socket/timer primitives
are swapped.

```
 Python test bed (client)                 Host simulator (server)
┌────────────────────────┐               ┌──────────────────────────────┐
│ transport.py           │               │ test/host_sim/main.cpp        │
│  Plaintext / Noise      │   TCP 6053    │ ─ uses ─                      │
│ client.py (lifecycle)  │◄─────────────►│ lib/ESPHomeAPI  (REAL code)   │
│ tests.py (TS-*)        │  localhost    │  api_server.cpp               │
│ runner.py (matrix)     │               │  api_connection.cpp           │
└────────────────────────┘               │  entity.cpp                   │
                                         │ ─ shim ─                       │
                                         │  host_sim/Arduino.h           │
                                         │  host_sim/host_wifi.h (sockets)│
                                         └──────────────────────────────┘
```

The library headers select the transport at compile time:

```cpp
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include "host_wifi.h"   // host simulator only
#endif
```

So the device build and the host build compile the *same* `.cpp` files; only
the WiFi/timer primitives differ. This is what makes L3 a genuine integration
test of the firmware rather than a mock.

Run it:

```bash
# 1. build the simulator from the real firmware sources
#    (compile the C crypto unit first, then link it with the C++ sources)
cd firmware
cc  -O2 -c lib/ESPHomeAPI/src/crypto/tweetnacl.c -o /tmp/tweetnacl.o
c++ -std=c++17 -I lib/ESPHomeAPI/src -I test/host_sim \
    test/host_sim/main.cpp \
    lib/ESPHomeAPI/src/api_connection.cpp \
    lib/ESPHomeAPI/src/api_server.cpp \
    lib/ESPHomeAPI/src/entity.cpp \
    lib/ESPHomeAPI/src/serial_log_bridge.cpp \
    lib/ESPHomeAPI/src/crypto/tweetnacl_glue.cpp /tmp/tweetnacl.o \
    -o test/host_sim/sim

# 2a. start it in PLAINTEXT mode (listens on 0.0.0.0:6053, password "testpass")
./test/host_sim/sim &

#     ...or 2b. start it in NOISE mode with a base64 PSK
SIM_ENCRYPTION_KEY="AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=" ./test/host_sim/sim &

# 3a. run the full suite (plaintext)
cd ../testbed
python run_tests.py --host 127.0.0.1 --password testpass \
    --allow-disruptive --out fw_results

# 3b. ...or run it over Noise (matching --encryption-key)
python run_tests.py --host 127.0.0.1 --password testpass \
    --encryption-key "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=" \
    --allow-disruptive --out fw_noise_results

# 4. stop the simulator
pkill -f host_sim/sim   # or: kill the printed PID
```

> Note: build the simulator with the crypto sources too — compile
> `crypto/tweetnacl.c` with a C compiler and link it with
> `crypto/tweetnacl_glue.cpp` and the library `.cpp` files (see the build
> command in `firmware/test/host_sim/main.cpp`).

### L4 — Home Assistant interop (manual)

The final confidence check: add the device (real ESP32 or the host simulator)
to Home Assistant by IP, port 6053, blank encryption key, and confirm entities
appear and can be controlled. This validates behavior against the real
production client, not just our test bed.

---

## 4. Test taxonomy

Every test maps to a `TS-*` ID from [`COMPLIANCE_TEST_PLAN.md`](COMPLIANCE_TEST_PLAN.md)
and is tagged with a **mode** and a **conformance level**.

### Modes (when a test is eligible to run)

| Mode | Needs | Behavior |
|---|---|---|
| `offline` | nothing | Always runs (codec/framing/hash/parsing). |
| `online` | reachable device + key/password | Runs against a device (real or simulator). Skipped with `--offline-only` or if no `--host`. |
| `disruptive` | device + `--allow-disruptive` | May trigger device-side actions (e.g. the restart button). Skipped unless explicitly allowed. |

### Conformance levels (RFC 2119)

`MUST`, `SHOULD`, `MAY`. The runner's verdict is driven by `MUST`:

```
MUST failures == 0 and MUST executed > 0   → CONFORMANT
   (and SHOULD all pass)                    → CONFORMANT
   (some SHOULD fail)                        → CONFORMANT-WITH-DEVIATIONS
MUST failures > 0                            → NON-CONFORMANT
no MUST executed                             → INCONCLUSIVE
```

### Status values

`PASS`, `FAIL`, `SKIP`. `SKIP` is used for not-applicable cases (e.g. Noise
tests against a plaintext device) and is **not** a failure.

---

## 5. Coverage map

Implemented `TS-*` IDs and where they run:

| Suite | IDs | Layer |
|---|---|---|
| Transport — plaintext framing | TS-1.2, 1.4, 1.5 | L2 offline |
| Transport — Noise | TS-2.3, 2.5, 2.6, 2.10 | L3 (encrypted device/sim) |
| Connection lifecycle | TS-3.2, 3.4, 3.5 | L3 online |
| Authentication | TS-4.5, 4.6 | L3 online |
| Keepalive | TS-5.2 | L3 online |
| Entity discovery | TS-6.3, 6.4, 6.5 + FNV vector | L3 online + L2 offline |
| State subscription | TS-7.2, 7.4 | L3 online |
| Commands | TS-8.3 | L3 disruptive |
| Protobuf encoding | TS-9.1–9.6, 9-HELLO | L2 offline |
| Logging (subscribe + stream) | TS-10-LOG | L3 online |
| Home Assistant helpers | TS-13-HA-SVC, TS-13-HA-STATE | L3 online |
| HA pairing & discovery | TS-15.1–15.5 | L3 online (+ zeroconf for mDNS) |
| Disconnect | TS-11.1 | L3 online |
| Robustness | TS-12.1, 12.2, 12.3, 12.7 | L3 online + L2 offline |
| Switch `restore_mode` persistence | host_test policy cases | L2 offline |

The firmware implements both plaintext and Noise, so the suite can be run in
either mode against the host simulator. Set `SIM_ENCRYPTION_KEY` (base64 PSK) on
the simulator and pass `--encryption-key` to the test bed to exercise the Noise
path (TS-2.x). In Noise mode the plaintext-indicator test (TS-12.2) is N/A and
skips; in plaintext mode the Noise tests (TS-2.x) skip.

### Latest results against the firmware (host simulator, L3)

Plaintext mode (`--allow-disruptive`):

```
MUST passed: 34/34   (failures: 0)
Skipped: 5           (TS-2.x Noise tests; TS-15.2 plaintext reject; TS-15.4/15.5 without mdns_name)
Overall: CONFORMANT
```

Noise mode (`SIM_ENCRYPTION_KEY` set, `--encryption-key` + `--allow-disruptive`):

```
MUST passed: 38/38   (failures: 0)
Skipped: 2           (TS-12.2 plaintext indicator; TS-15.4/15.5 without mdns_name)
Overall: CONFORMANT
```

---

## 6. Why the host simulator (design rationale)

- **No mocks of protocol logic.** The simulator links the production `.cpp`
  files. A framing or encoding regression in the firmware is caught here, not
  hidden behind a test double.
- **Hardware-independent CI.** Full lifecycle/discovery/state/command coverage
  runs on any POSIX host.
- **Fast.** Build + full suite complete in seconds.
- **Faithful boundary.** Only the Arduino `WiFiServer`/`WiFiClient`/`millis()`
  surface is shimmed — the smallest possible seam — using real BSD sockets, so
  byte ordering, partial reads, and connection teardown behave like the device.

Trade-off: the simulator does **not** validate hardware-specific concerns
(WiFi stack behavior, RAM pressure under load, watchdog timing). Those are
covered by L1 (build/size) and L4 (manual on-device + HA).

---

## 7. Running everything (summary)

```bash
# L1 build
cd firmware && pio run -e esp32dev -e esp32-c3 -e nodemcuv2

# L2 offline unit tests (both codecs)
cd ../testbed && python run_tests.py --offline-only
cd ../firmware/test && c++ -std=c++17 -I ../lib/ESPHomeAPI/src host_test.cpp -o host_test && ./host_test

# L3 integration (firmware code vs test bed)
cd .. && cc -O2 -c lib/ESPHomeAPI/src/crypto/tweetnacl.c -o /tmp/tweetnacl.o
c++ -std=c++17 -I lib/ESPHomeAPI/src -I test/host_sim \
    test/host_sim/main.cpp lib/ESPHomeAPI/src/*.cpp \
    lib/ESPHomeAPI/src/crypto/tweetnacl_glue.cpp /tmp/tweetnacl.o \
    -o test/host_sim/sim
./test/host_sim/sim & SIM=$!
cd ../testbed && python run_tests.py --host 127.0.0.1 --password testpass --allow-disruptive --out fw_results
kill $SIM
```

Exit codes are non-zero on any `MUST` failure, so each layer can gate a CI
pipeline. The test bed additionally writes `fw_results.json` and
`fw_results.md` for archiving.

---

## 8. Extending the tests

- **New protocol feature in the firmware** → add/realize the matching `TS-*`
  case in `testbed/esphome_testbed/tests.py`, expose any needed message in
  `client.py`, and (if it has a stable byte form) add an offline vector to both
  `tests.py` and `firmware/test/host_test.cpp`.
- **New entity type** → add it to `host_sim/main.cpp` so discovery/state tests
  cover it.
- **Noise** → implemented on both sides: the test bed client
  (`transport.NoiseTransport`) and the firmware responder (`crypto/noise.h`).
  Run the simulator with `SIM_ENCRYPTION_KEY` and the test bed with
  `--encryption-key` to exercise the TS-2.x suite and the full lifecycle over
  the encrypted transport.
