# Compliance Test Run Results

Generated: 2026-06-09 01:31:30

| Test ID | Level | Mode | Status | Note |
|---|---|---|---|---|
| TS-1.2 | MUST | offline | PASS | framed/parsed length=3 type=7 |
| TS-1.4 | MUST | offline | PASS | len varint = ac02 |
| TS-1.5 | MUST | offline | PASS | 00 00 07 |
| TS-2.5 | MUST | online | PASS | handshake ok, node=sim-device mac=AA:BB:CC:DD:EE:F0 |
| TS-2.3 | MUST | online | PASS | name=sim-device mac=AA:BB:CC:DD:EE:F0 |
| TS-2.10 | MUST | online | PASS | rejected as expected: connection closed by peer |
| TS-2.6 | MUST | offline | PASS | 32-byte PSK |
| TS-3.2 | MUST | online | PASS | API 1.10, name=sim-device |
| TS-3.4 | MUST | online | PASS | authenticated |
| TS-3.5 | MUST | online | PASS | server major=1 (compatible); policy applies on mismatch |
| TS-4.5 | MUST | online | PASS | rejected wrong password |
| TS-4.6 | MUST | online | PASS | server closed/blocked as expected: connection closed by peer |
| TS-5.2 | MUST | online | PASS | ping/pong ok |
| TS-6.5 | MUST | online | PASS | discovered 5 entities, Done received |
| TS-6.4 | MUST | online | PASS | verified 5 keys via FNV-1 |
| TS-6.3 | MUST | online | PASS | 5 entities have required fields |
| TS-6.4-OFFLINE | MUST | offline | PASS | 0xDB0A35EC |
| TS-7.2 | MUST | online | PASS | state sensor key=0x4EEE9F69 value=4.001999855041504 |
| TS-7.4 | MUST | online | PASS | correlated key 0x4EEE9F69 |
| TS-8.3 | MUST | disruptive | PASS | sent ButtonCommandRequest to restart (0x47BA7052) |
| TS-9.1 | MUST | offline | PASS | tags 0x0A/0x10/0x0D/0x15 |
| TS-9.2 | MUST | offline | PASS | 0D EC 35 0A DB |
| TS-9.2b | MUST | offline | PASS | 15 00 00 BC 41 -> 23.5 |
| TS-9.3 | MUST | offline | PASS | defaults omitted |
| TS-9.4 | MUST | offline | PASS | zigzag ok for boundary values |
| TS-9.5 | MUST | offline | PASS | decoded known field, retained 2 unknown fields |
| TS-9.6 | MUST | offline | PASS | utf-8 preserved |
| TS-9-HELLO | MUST | offline | PASS | 0a0842656e7475696e6f1001180a |
| TS-10-LOG | MUST | online | PASS | level=5 msg='sim heartbeat uptime=6s' |
| TS-13-HA-SVC | MUST | online | PASS | event='esphome.sim_heartbeat' data=[('uptime', '8')] |
| TS-13-HA-STATE | MUST | online | PASS | device consumed HA state: 'HA state sun.sun=above_horizon' |
| TS-11.1 | MUST | online | PASS | received DisconnectResponse (6) |
| TS-12.1 | MUST | online | PASS | unknown type ignored; ping still works |
| TS-12.3 | MUST | offline | PASS | raised cleanly: truncated varint |
| TS-12.7 | MUST | offline | PASS | handled short length-delimited field |
| TS-12.2 | MUST | online | SKIP | device uses Noise; plaintext indicator test N/A |
