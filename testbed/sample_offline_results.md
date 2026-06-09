# Compliance Test Run Results

Generated: 2026-06-08 23:41:47

| Test ID | Level | Mode | Status | Note |
|---|---|---|---|---|
| TS-1.2 | MUST | offline | PASS | framed/parsed length=3 type=7 |
| TS-1.4 | MUST | offline | PASS | len varint = ac02 |
| TS-1.5 | MUST | offline | PASS | 00 00 07 |
| TS-2.5 | MUST | online | SKIP | device not available / offline-only |
| TS-2.3 | MUST | online | SKIP | device not available / offline-only |
| TS-2.10 | MUST | online | SKIP | device not available / offline-only |
| TS-2.6 | MUST | offline | SKIP | no encryption key configured |
| TS-3.2 | MUST | online | SKIP | device not available / offline-only |
| TS-3.4 | MUST | online | SKIP | device not available / offline-only |
| TS-3.5 | MUST | online | SKIP | device not available / offline-only |
| TS-4.5 | MUST | online | SKIP | device not available / offline-only |
| TS-4.6 | MUST | online | SKIP | device not available / offline-only |
| TS-5.2 | MUST | online | SKIP | device not available / offline-only |
| TS-6.5 | MUST | online | SKIP | device not available / offline-only |
| TS-6.4 | MUST | online | SKIP | device not available / offline-only |
| TS-6.3 | MUST | online | SKIP | device not available / offline-only |
| TS-6.4-OFFLINE | MUST | offline | PASS | 0xDB0A35EC |
| TS-7.2 | MUST | online | SKIP | device not available / offline-only |
| TS-7.4 | MUST | online | SKIP | device not available / offline-only |
| TS-8.3 | MUST | disruptive | SKIP | device not available / offline-only |
| TS-9.1 | MUST | offline | PASS | tags 0x0A/0x10/0x0D/0x15 |
| TS-9.2 | MUST | offline | PASS | 0D EC 35 0A DB |
| TS-9.2b | MUST | offline | PASS | 15 00 00 BC 41 -> 23.5 |
| TS-9.3 | MUST | offline | PASS | defaults omitted |
| TS-9.4 | MUST | offline | PASS | zigzag ok for boundary values |
| TS-9.5 | MUST | offline | PASS | decoded known field, retained 2 unknown fields |
| TS-9.6 | MUST | offline | PASS | utf-8 preserved |
| TS-9-HELLO | MUST | offline | PASS | 0a0842656e7475696e6f1001180a |
| TS-11.1 | MUST | online | SKIP | device not available / offline-only |
| TS-12.1 | MUST | online | SKIP | device not available / offline-only |
| TS-12.3 | MUST | offline | PASS | raised cleanly: truncated varint |
| TS-12.7 | MUST | offline | PASS | handled short length-delimited field |
| TS-12.2 | MUST | online | SKIP | device not available / offline-only |
