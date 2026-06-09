# ESPHome Native API — Compliance Test Bed

A runnable, dependency-light Python harness that executes the cases defined in
[`../COMPLIANCE_TEST_PLAN.md`](../COMPLIANCE_TEST_PLAN.md) against a real
ESPHome device (CLIENT role — the harness behaves like Home Assistant and
exercises the device's server implementation).

It also includes a set of **offline** unit tests that validate framing,
Protobuf wire-format encoding, ZigZag, and the FNV-1 entity-key hash without
any device.

## What it implements

- Plaintext framing (`0x00` indicator) — `transport.PlaintextTransport`
- Noise framing + `Noise_NNpsk0_25519_ChaChaPoly_SHA256` handshake —
  `transport.NoiseTransport` (empty client hello, server-hello parse,
  prologue `NoiseAPIInit\x00\x00`, encrypted data frames)
- A self-contained Protobuf codec (no `protobuf`/`protoc` needed) — `proto.py`
- Session lifecycle, discovery, state, commands — `client.APIClient`
- A registry-based runner that emits a pass/fail matrix — `runner.py`
- Tests mapped to `TS-*` IDs — `tests.py`

## Install

Offline tests need only the standard library. For encrypted (online) tests:

```bash
pip install -r requirements.txt   # installs noiseprotocol
```

## Usage

Offline tests only (no device):

```bash
python run_tests.py --offline-only
```

Against an **encrypted** device (your kmeter uses Noise):

```bash
python run_tests.py --host 192.168.1.50 \
  --encryption-key "PASTE_BASE64_API_KEY==" \
  --out results
```

Against a **plaintext + password** device:

```bash
python run_tests.py --host 192.168.1.50 --password mypass --out results
```

Home Assistant pairing regressions (TS-15) against a passwordless device:

```bash
python run_tests.py --host 192.168.1.42 --password "" \
  --mdns-name thermal-solar-supervisor-api \
  --expected-manufacturer M5Stack --out tss_plaintext
```

mDNS tests (TS-15.4/15.5) need `zeroconf` (`pip install -r requirements.txt`) and
`--mdns-name` matching the ESPHome node name.

Using a config file (copy `config.example.json`):

```bash
python run_tests.py --config config.json --out results
```

Run a single suite, e.g. all Protobuf encoding tests:

```bash
python run_tests.py --only TS-9 --offline-only
```

Allow tests that may **reboot** the device (e.g. pressing the `restart` button):

```bash
python run_tests.py --config config.json --allow-disruptive
```

## Output

The runner prints a colored matrix and a conformance verdict, and (with
`--out NAME`) writes `NAME.json` and `NAME.md`. Exit code is non-zero if any
`MUST` test fails (useful for CI). A sample offline run is included as
`sample_offline_results.md`.

```
TEST            LEVEL   STATUS  NOTE
TS-9.2          MUST    PASS    0D EC 35 0A DB
TS-6.4          MUST    PASS    verified N keys via FNV-1
...
MUST   passed: 14/14   (failures: 0)
Overall: CONFORMANT
```

## Test modes

| Mode | Needs | Notes |
|------|-------|-------|
| `offline` | nothing | framing, protobuf, ZigZag, FNV-1, robustness parsing |
| `online` | reachable device + key/password | handshake, lifecycle, discovery, state, ping, disconnect |
| `disruptive` | device + `--allow-disruptive` | may trigger actions that reboot the device |

## Coverage

Implemented test IDs: TS-1.2/1.4/1.5, TS-2.3/2.5/2.6/2.10, TS-3.2/3.4/3.5,
TS-4.5/4.6, TS-5.2, TS-6.3/6.4/6.5, TS-7.2/7.4, TS-8.3, TS-9.1–9.6 (+vectors),
TS-10-LOG (log streaming), TS-13-HA-SVC / TS-13-HA-STATE (Home Assistant
service/event + state subscription helpers), TS-11.1,
TS-12.1/12.2/12.3/12.7, TS-15.1–15.5 (HA pairing: Hello-only discovery,
RequiresEncryption reject, DeviceInfo field 12, mDNS). Remaining cases in the
plan are either covered implicitly or require server-role / fault-injection
setups noted in the plan.

## Layout

```
testbed/
  run_tests.py            # CLI entry point
  requirements.txt
  config.example.json
  esphome_testbed/
    proto.py              # varint / fixed32 / zigzag / decoder + FNV-1
    messages.py           # message-type IDs and entity maps
    transport.py          # plaintext + Noise transports
    client.py             # APIClient (lifecycle, discovery, state, commands)
    runner.py             # registry, execution context, reporting
    tests.py              # TS-* test implementations
```
