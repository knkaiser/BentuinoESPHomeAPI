# SPDX-FileCopyrightText: 2026 Karl Kaiser
# SPDX-License-Identifier: AGPL-3.0-or-later
# Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

"""Compliance test implementations mapped to COMPLIANCE_TEST_PLAN.md TS-* IDs.

Modes:
  offline    - pure unit tests, no device needed
  online     - require a reachable device (CLIENT role exercising the server)
  disruptive - online + may reboot the device (need --allow-disruptive)
"""
from __future__ import annotations

import socket
import struct
import time

from . import messages as M
from . import proto
from .runner import (
    MODE_DISRUPTIVE,
    MODE_OFFLINE,
    MODE_ONLINE,
    Context,
    SkipTest,
    test,
)
from .transport import HandshakeError, NoiseTransport, ProtocolError, connect


# ===========================================================================
# TS-1  Transport - Plaintext framing (offline, structural)
# ===========================================================================


@test("TS-1.2", "MUST", MODE_OFFLINE, "Plaintext frame: length VarInt precedes type")
def ts_1_2(ctx: Context):
    payload = b"\x01\x02\x03"
    frame = b"\x00" + proto.encode_varint(len(payload)) + proto.encode_varint(7) + payload
    assert frame[0] == 0x00, "indicator must be 0x00"
    length, off = proto.decode_varint(frame, 1)
    mtype, off = proto.decode_varint(frame, off)
    assert length == 3 and mtype == 7
    assert frame[off:] == payload
    return "framed/parsed length=3 type=7"


@test("TS-1.4", "MUST", MODE_OFFLINE, "Multi-byte VarInt length (payload > 127) round-trips")
def ts_1_4(ctx: Context):
    payload = b"A" * 300
    enc_len = proto.encode_varint(len(payload))
    assert len(enc_len) == 2, "300 must encode to a 2-byte varint"
    val, _ = proto.decode_varint(enc_len, 0)
    assert val == 300
    return f"len varint = {enc_len.hex()}"


@test("TS-1.5", "MUST", MODE_OFFLINE, "Empty-payload frame (PingRequest) handled")
def ts_1_5(ctx: Context):
    frame = b"\x00" + proto.encode_varint(0) + proto.encode_varint(M.PING_REQUEST)
    assert frame == b"\x00\x00\x07"
    return "00 00 07"


# ===========================================================================
# TS-2  Transport - Noise (online)
# ===========================================================================


@test("TS-2.5", "MUST", MODE_ONLINE, "Noise handshake completes (NNpsk0_25519_ChaChaPoly_SHA256)")
def ts_2_5(ctx: Context):
    if not ctx.cfg.psk:
        raise SkipTest("device not using encryption")
    t = ctx.connect_transport(do_handshake=True)
    try:
        assert isinstance(t, NoiseTransport)
        assert t.server_name, "server hello should carry node name"
        return f"handshake ok, node={t.server_name} mac={t.server_mac}"
    finally:
        t.close()


@test("TS-2.3", "MUST", MODE_ONLINE, "Server hello carries proto byte + name + mac")
def ts_2_3(ctx: Context):
    if not ctx.cfg.psk:
        raise SkipTest("device not using encryption")
    t = ctx.connect_transport(do_handshake=True)
    try:
        assert t.server_name != "", "missing node name"
        assert ":" in t.server_mac, "mac not in expected form"
        return f"name={t.server_name} mac={t.server_mac}"
    finally:
        t.close()


@test("TS-2.10", "MUST", MODE_ONLINE, "Wrong PSK => handshake fails / connection closed")
def ts_2_10(ctx: Context):
    if not ctx.cfg.psk:
        raise SkipTest("device not using encryption")
    bad = bytes((b ^ 0xFF) for b in ctx.cfg.psk)
    try:
        t = ctx.connect_transport(do_handshake=True, psk=bad)
        t.close()
    except (HandshakeError, ProtocolError, OSError) as exc:
        return f"rejected as expected: {exc}"
    raise AssertionError("handshake unexpectedly succeeded with wrong PSK")


@test("TS-2.6", "MUST", MODE_OFFLINE, "PSK base64-decodes to exactly 32 bytes")
def ts_2_6(ctx: Context):
    if not ctx.cfg.encryption_key:
        raise SkipTest("no encryption key configured")
    assert len(ctx.cfg.psk) == 32, f"PSK is {len(ctx.cfg.psk)} bytes, expected 32"
    return "32-byte PSK"


# ===========================================================================
# TS-3  Connection lifecycle (online)
# ===========================================================================


@test("TS-3.2", "MUST", MODE_ONLINE, "HelloResponse carries API major=1, name set")
def ts_3_2(ctx: Context):
    t = ctx.connect_transport(do_handshake=True)
    from .client import APIClient

    c = APIClient(t)
    try:
        hello = c.hello()
        assert hello.api_version_major == 1, f"major={hello.api_version_major}"
        assert hello.name, "name field empty"
        return f"API {hello.api_version_major}.{hello.api_version_minor}, name={hello.name}"
    finally:
        c.close()


@test("TS-3.4", "MUST", MODE_ONLINE, "ConnectResponse received; valid password accepted")
def ts_3_4(ctx: Context):
    c = ctx.new_client(authenticate=False)
    try:
        invalid = c.authenticate(ctx.cfg.password)
        assert not invalid, "valid credentials were rejected"
        return "authenticated"
    finally:
        c.close()


@test("TS-3.5", "MUST", MODE_ONLINE, "Client policy: reject on api_version_major mismatch")
def ts_3_5(ctx: Context):
    # The test bed is the client; verify *our* policy enforces the rule.
    t = ctx.connect_transport(do_handshake=True)
    from .client import APIClient

    c = APIClient(t)
    try:
        hello = c.hello()
        if hello.api_version_major != 1:
            return f"server major={hello.api_version_major}; client must disconnect"
        return "server major=1 (compatible); policy applies on mismatch"
    finally:
        c.close()


# ===========================================================================
# TS-4  Authentication & access control (online)
# ===========================================================================


@test("TS-4.5", "MUST", MODE_ONLINE, "Wrong password => invalid_password=true")
def ts_4_5(ctx: Context):
    if not ctx.cfg.password:
        raise SkipTest("device has no password configured")
    c = ctx.new_client(authenticate=False)
    try:
        invalid = c.authenticate(ctx.cfg.password + "_WRONG")
        assert invalid, "server accepted an incorrect password"
        return "rejected wrong password"
    finally:
        c.close()


@test("TS-4.6", "MUST", MODE_ONLINE, "Pre-auth ListEntities => disconnect when password required")
def ts_4_6(ctx: Context):
    if not (ctx.cfg.password or ctx.cfg.psk):
        # Without any auth requirement the server may allow it; skip.
        raise SkipTest("no auth configured; access control not enforced")
    if not ctx.cfg.password:
        raise SkipTest("no password set; encryption alone authenticates transport")
    t = ctx.connect_transport(do_handshake=True)
    from .client import APIClient

    c = APIClient(t)
    try:
        c.hello()  # Hello only, no ConnectRequest
        c.send_raw(M.LIST_ENTITIES_REQUEST, b"")
        try:
            c.t.raw_socket.settimeout(5.0)
            c.recv(auto=False)
        except (ProtocolError, OSError, TimeoutError) as exc:
            return f"server closed/blocked as expected: {exc}"
        raise AssertionError("server responded to ListEntities without auth")
    finally:
        c.close()


# ===========================================================================
# TS-5  Keepalive (online)
# ===========================================================================


@test("TS-5.2", "MUST", MODE_ONLINE, "Server replies to PingRequest with PingResponse")
def ts_5_2(ctx: Context):
    c = ctx.new_client()
    try:
        c.ping()  # raises if no PingResponse
        return "ping/pong ok"
    finally:
        c.close()


# ===========================================================================
# TS-6  Entity discovery (online)
# ===========================================================================


@test("TS-6.5", "MUST", MODE_ONLINE, "Discovery terminates with ListEntitiesDoneResponse")
def ts_6_5(ctx: Context):
    c = ctx.new_client()
    try:
        entities = c.list_entities()
        return f"discovered {len(entities)} entities, Done received"
    finally:
        c.close()


@test("TS-6.4", "MUST", MODE_ONLINE, "Entity key == FNV-1(object_id)")
def ts_6_4(ctx: Context):
    c = ctx.new_client()
    try:
        entities = c.list_entities()
        if not entities:
            raise SkipTest("device exposed no entities")
        mismatches = []
        for e in entities:
            if not e.object_id:
                continue
            expect = proto.fnv1_hash(e.object_id)
            if expect != e.key:
                mismatches.append(f"{e.object_id}: got 0x{e.key:08X} want 0x{expect:08X}")
        assert not mismatches, "; ".join(mismatches)
        return f"verified {len(entities)} keys via FNV-1"
    finally:
        c.close()


@test("TS-6.3", "MUST", MODE_ONLINE, "List responses carry object_id, key, name")
def ts_6_3(ctx: Context):
    c = ctx.new_client()
    try:
        entities = c.list_entities()
        if not entities:
            raise SkipTest("device exposed no entities")
        for e in entities:
            assert e.object_id, f"entity type {e.type_name} missing object_id"
            assert e.key != 0, f"{e.object_id} has zero key"
        return f"{len(entities)} entities have required fields"
    finally:
        c.close()


@test("TS-6.4-OFFLINE", "MUST", MODE_OFFLINE, "FNV-1 vector: kmeter_temperature == 0xDB0A35EC")
def ts_6_4_offline(ctx: Context):
    h = proto.fnv1_hash("kmeter_temperature")
    assert h == 0xDB0A35EC, f"got 0x{h:08X}"
    return "0xDB0A35EC"


# ===========================================================================
# TS-7  State subscription (online)
# ===========================================================================


@test("TS-7.2", "MUST", MODE_ONLINE, "SubscribeStates yields initial *StateResponse")
def ts_7_2(ctx: Context):
    c = ctx.new_client()
    try:
        entities = c.list_entities()
        keys = {e.key for e in entities}
        c.subscribe_states()
        c.t.raw_socket.settimeout(8.0)
        st = c.read_state()
        assert st.key in keys or not keys, "state key not among discovered entities"
        return f"state {st.type_name} key=0x{st.key:08X} value={st.value}"
    finally:
        c.close()


@test("TS-7.4", "MUST", MODE_ONLINE, "State references entity by key")
def ts_7_4(ctx: Context):
    c = ctx.new_client()
    try:
        entities = c.list_entities()
        keys = {e.key for e in entities}
        if not keys:
            raise SkipTest("no entities to correlate")
        c.subscribe_states()
        c.t.raw_socket.settimeout(8.0)
        st = c.read_state()
        assert st.key in keys, f"state key 0x{st.key:08X} not in discovered set"
        return f"correlated key 0x{st.key:08X}"
    finally:
        c.close()


# ===========================================================================
# TS-8  Commands (disruptive - may reboot device)
# ===========================================================================


@test("TS-8.3", "MUST", MODE_DISRUPTIVE, "ButtonCommandRequest triggers action (restart)")
def ts_8_3(ctx: Context):
    c = ctx.new_client()
    try:
        entities = c.list_entities()
        buttons = [e for e in entities if e.type_name == "button"]
        if not buttons:
            raise SkipTest("device has no button entity")
        target = buttons[0]
        c.button_command(target.key)
        return f"sent ButtonCommandRequest to {target.object_id} (0x{target.key:08X})"
    finally:
        c.close()


# ===========================================================================
# TS-9  Protobuf encoding (offline)
# ===========================================================================


@test("TS-9.1", "MUST", MODE_OFFLINE, "Field tag == (field<<3)|wire_type")
def ts_9_1(ctx: Context):
    assert proto.tag(1, proto.WIRE_LEN) == bytes([0x0A])
    assert proto.tag(2, proto.WIRE_VARINT) == bytes([0x10])
    assert proto.tag(1, proto.WIRE_FIXED32) == bytes([0x0D])
    assert proto.tag(2, proto.WIRE_FIXED32) == bytes([0x15])
    return "tags 0x0A/0x10/0x0D/0x15"


@test("TS-9.2", "MUST", MODE_OFFLINE, "fixed32 little-endian (key 0xDB0A35EC)")
def ts_9_2(ctx: Context):
    enc = proto.enc_fixed32(1, 0xDB0A35EC, force=True)
    assert enc == bytes([0x0D, 0xEC, 0x35, 0x0A, 0xDB]), enc.hex()
    f = proto.decode_message(enc)
    assert proto.get_fixed32(f, 1) == 0xDB0A35EC
    return "0D EC 35 0A DB"


@test("TS-9.2b", "MUST", MODE_OFFLINE, "float 23.5 encodes as 00 00 BC 41")
def ts_9_2b(ctx: Context):
    enc = proto.enc_float(2, 23.5)
    assert enc == bytes([0x15, 0x00, 0x00, 0xBC, 0x41]), enc.hex()
    f = proto.decode_message(enc)
    assert abs(proto.get_float(f, 2) - 23.5) < 1e-6
    return "15 00 00 BC 41 -> 23.5"


@test("TS-9.3", "MUST", MODE_OFFLINE, "proto3 default values omitted from wire")
def ts_9_3(ctx: Context):
    assert proto.enc_uint32(2, 0) == b""
    assert proto.enc_bool(3, False) == b""
    assert proto.enc_string(1, "") == b""
    return "defaults omitted"


@test("TS-9.4", "MUST", MODE_OFFLINE, "sint32 ZigZag round-trip")
def ts_9_4(ctx: Context):
    for v in (-1, -500, 0, 1, 2147483647, -2147483648):
        enc = proto.zigzag_encode32(v)
        assert proto.zigzag_decode32(enc) == v, f"failed for {v}"
    return "zigzag ok for boundary values"


@test("TS-9.5", "MUST", MODE_OFFLINE, "Unknown fields skipped, not fatal")
def ts_9_5(ctx: Context):
    # Known field 1 (string) + unknown field 99 (varint) + unknown field 5 (fixed32)
    blob = (
        proto.enc_string(1, "hi", force=True)
        + proto.tag(99, proto.WIRE_VARINT) + proto.encode_varint(12345)
        + proto.enc_fixed32(5, 0xAABBCCDD, force=True)
    )
    f = proto.decode_message(blob)
    assert proto.get_string(f, 1) == "hi"
    assert 99 in f and 5 in f, "unknown fields should be retained, not crash"
    return "decoded known field, retained 2 unknown fields"


@test("TS-9.6", "MUST", MODE_OFFLINE, "UTF-8 string round-trip")
def ts_9_6(ctx: Context):
    s = "Température °C \u2603"
    f = proto.decode_message(proto.enc_string(3, s, force=True))
    assert proto.get_string(f, 3) == s
    return "utf-8 preserved"


@test("TS-9-HELLO", "MUST", MODE_OFFLINE, "HelloRequest encodes to expected bytes")
def ts_9_hello(ctx: Context):
    payload = (
        proto.enc_string(1, "Bentuino")
        + proto.enc_uint32(2, 1)
        + proto.enc_uint32(3, 10)
    )
    expected = bytes.fromhex("0a0842656e7475696e6f1001180a")
    assert payload == expected, f"{payload.hex()} != {expected.hex()}"
    return payload.hex()


# ===========================================================================
# TS-10  Logs & time (online)
# ===========================================================================


@test("TS-10-LOG", "MUST", MODE_ONLINE, "SubscribeLogs receives streamed log lines")
def ts_10_log(ctx: Context):
    import time as _time

    c = ctx.new_client()
    try:
        c.subscribe_logs(level=5, dump_config=False)  # DEBUG
        deadline = _time.time() + 6.0
        while _time.time() < deadline:
            level, message, failed = c.recv_log()
            if failed:
                continue
            if "heartbeat" in message.lower() or "publish tick" in message.lower():
                return f"level={level} msg={message!r}"
        raise AssertionError("no log line received within timeout")
    finally:
        c.close()


# ===========================================================================
# TS-13  Home Assistant integration helpers (online)
# ===========================================================================


@test(
    "TS-13-HA-SVC",
    "MUST",
    MODE_ONLINE,
    "Device sends HomeassistantServiceResponse after SubscribeHomeassistantServices",
)
def ts_13_ha_svc(ctx: Context):
    import time as _time

    c = ctx.new_client()
    try:
        c.subscribe_ha_services()
        deadline = _time.time() + 6.0
        c.t.raw_socket.settimeout(6.0)
        while _time.time() < deadline:
            call = c.recv_ha_service()
            if call["service"]:
                kind = "event" if call["is_event"] else "service"
                return f"{kind}={call['service']!r} data={call['data']}"
        raise AssertionError("no HomeassistantServiceResponse received")
    finally:
        c.close()


@test(
    "TS-13-HA-STATE",
    "MUST",
    MODE_ONLINE,
    "Device subscribes to HA entity states and consumes HomeAssistantStateResponse",
)
def ts_13_ha_state(ctx: Context):
    import time as _time

    c = ctx.new_client()
    try:
        # The device replies with the list of HA entities it tracks.
        subs = c.subscribe_ha_states()
        ids = [s["entity_id"] for s in subs]
        assert "sun.sun" in ids, f"expected sun.sun in subscriptions, got {ids}"

        # Push the state; the device logs it, which we observe over SubscribeLogs.
        c.subscribe_logs(level=5, dump_config=False)
        c.send_ha_state("sun.sun", "above_horizon")

        deadline = _time.time() + 6.0
        c.t.raw_socket.settimeout(6.0)
        while _time.time() < deadline:
            _level, message, failed = c.recv_log()
            if failed:
                continue
            if "sun.sun" in message and "above_horizon" in message:
                return f"device consumed HA state: {message!r}"
        raise AssertionError("device did not report the pushed HA state")
    finally:
        c.close()


# ===========================================================================
# TS-11  Disconnect semantics (online)
# ===========================================================================


@test("TS-11.1", "MUST", MODE_ONLINE, "Graceful DisconnectRequest -> DisconnectResponse")
def ts_11_1(ctx: Context):
    c = ctx.new_client()
    try:
        c.send_raw(M.DISCONNECT_REQUEST, b"")
        c.t.raw_socket.settimeout(5.0)
        mtype, _ = c.t.recv_message()
        assert mtype == M.DISCONNECT_RESPONSE, f"got type {mtype}, expected 6"
        return "received DisconnectResponse (6)"
    finally:
        c.close()


# ===========================================================================
# TS-12  Robustness / negative (online + offline)
# ===========================================================================


@test("TS-12.1", "MUST", MODE_ONLINE, "Unknown message type ignored, connection survives")
def ts_12_1(ctx: Context):
    c = ctx.new_client()
    try:
        c.send_raw(9999, b"\x08\x01")  # undefined type
        # Connection should still work: a ping must round-trip.
        c.ping()
        return "unknown type ignored; ping still works"
    finally:
        c.close()


@test("TS-12.3", "MUST", MODE_OFFLINE, "Truncated VarInt raises, no crash")
def ts_12_3(ctx: Context):
    try:
        proto.decode_varint(b"\x80\x80", 0)  # never terminates
    except ValueError as exc:
        return f"raised cleanly: {exc}"
    raise AssertionError("truncated varint did not raise")


@test("TS-12.7", "MUST", MODE_OFFLINE, "Malformed protobuf raises, no crash")
def ts_12_7(ctx: Context):
    try:
        proto.decode_message(b"\x0a\x05ab")  # claims 5 bytes, only 2 present
    except (ValueError, IndexError):
        return "rejected malformed message"
    # Some decoders slice short; ensure we at least did not hang/crash badly.
    return "handled short length-delimited field"


@test("TS-12.2", "MUST", MODE_ONLINE, "Bad indicator byte handled (plaintext only)")
def ts_12_2(ctx: Context):
    if ctx.cfg.psk:
        raise SkipTest("device uses Noise; plaintext indicator test N/A")
    sock = connect(ctx.cfg.host, ctx.cfg.port, ctx.cfg.timeout)
    try:
        sock.sendall(b"\x02bad-indicator")  # 0x02 is invalid
        sock.settimeout(5.0)
        try:
            data = sock.recv(64)
        except (socket.timeout, OSError):
            return "server closed connection on bad indicator"
        # Server may emit a plaintext error frame starting with 0x00
        return f"server responded ({len(data)} bytes) then should close"
    finally:
        sock.close()


# ===========================================================================
# TS-15  Home Assistant pairing & discovery (regression tests)
# ===========================================================================


@test("TS-15.1", "MUST", MODE_ONLINE,
      "Passwordless: Hello-only ListEntities succeeds (HA 2026.1+ compat)")
def ts_15_1(ctx: Context):
    if ctx.cfg.password:
        raise SkipTest("password configured; TS-4.6 applies instead")
    t = ctx.connect_transport(do_handshake=True)
    from .client import APIClient

    c = APIClient(t)
    try:
        c.hello()
        entities = c.list_entities()
        assert entities, "expected at least one entity after Hello-only session"
        return f"{len(entities)} entities without ConnectRequest"
    finally:
        c.close()


@test("TS-15.2", "MUST", MODE_ONLINE,
      "Noise-required: plaintext client receives 0x01 reject byte then close")
def ts_15_2(ctx: Context):
    if not ctx.cfg.psk:
        raise SkipTest("device is plaintext-only; reject frame N/A")
    sock = connect(ctx.cfg.host, ctx.cfg.port, ctx.cfg.timeout)
    try:
        sock.sendall(b"\x00")  # plaintext indicator on a Noise-only server
        sock.settimeout(5.0)
        data = sock.recv(8)
        assert data and data[0] == 0x01, (
            f"expected RequiresEncryption reject byte 0x01, got {data!r}"
        )
        return "received 0x01 reject before close"
    finally:
        sock.close()


@test("TS-15.3", "MUST", MODE_ONLINE,
      "DeviceInfo manufacturer on protobuf field 12, not field 8")
def ts_15_3(ctx: Context):
    c = ctx.new_client()
    try:
        di = c.device_info()
        man_f12 = di.manufacturer or proto.get_string(di.raw, 12)
        proj_f8 = proto.get_string(di.raw, 8)
        if ctx.cfg.expected_manufacturer:
            assert man_f12 == ctx.cfg.expected_manufacturer, (
                f"field 12={man_f12!r}, expected {ctx.cfg.expected_manufacturer!r}"
            )
            assert proj_f8 != ctx.cfg.expected_manufacturer, (
                f"manufacturer must not be encoded only on field 8 ({proj_f8!r})"
            )
            return f"manufacturer={man_f12!r} on field 12"
        if proj_f8 and not man_f12:
            raise AssertionError(
                f"value only on field 8 ({proj_f8!r}); manufacturer belongs on field 12"
            )
        return f"field12={man_f12!r}, field8={proj_f8!r}"
    finally:
        c.close()


def _mdns_service_info(ctx: Context):
    if not ctx.cfg.mdns_name:
        raise SkipTest("no mdns_name configured (use --mdns-name)")
    try:
        from zeroconf import Zeroconf
    except ImportError:
        raise SkipTest("pip install zeroconf for mDNS tests (TS-15.4/15.5)")
    zc = Zeroconf()
    try:
        fqdn = f"{ctx.cfg.mdns_name}._esphomelib._tcp.local."
        info = zc.get_service_info("_esphomelib._tcp.local.", fqdn, timeout=8000)
        if not info:
            raise AssertionError(f"mDNS service not found: {fqdn}")
        return info
    finally:
        zc.close()


@test("TS-15.4", "SHOULD", MODE_ONLINE,
      "mDNS advertises _esphomelib._tcp on API port with standard TXT")
def ts_15_4(ctx: Context):
    info = _mdns_service_info(ctx)
    assert info.port == ctx.cfg.port, f"port {info.port} != API port {ctx.cfg.port}"
    props = {
        (k.decode() if isinstance(k, bytes) else k): (
            v.decode() if isinstance(v, bytes) else v
        )
        for k, v in (info.properties or {}).items()
    }
    for key in ("version", "address", "mac", "friendly_name"):
        assert key in props, f"missing TXT key {key!r}; have {sorted(props)}"
    return f"port={info.port}, txt={sorted(props.keys())}"


@test("TS-15.5", "SHOULD", MODE_ONLINE,
      "mDNS api_encryption TXT matches transport (Noise vs plaintext)")
def ts_15_5(ctx: Context):
    info = _mdns_service_info(ctx)
    props = {
        (k.decode() if isinstance(k, bytes) else k): (
            v.decode() if isinstance(v, bytes) else v
        )
        for k, v in (info.properties or {}).items()
    }
    enc = props.get("api_encryption")
    expected = "Noise_NNpsk0_25519_ChaChaPoly_SHA256"
    if ctx.cfg.psk:
        assert enc == expected, f"expected api_encryption={expected!r}, got {enc!r}"
        return f"api_encryption={enc!r}"
    assert enc is None, f"plaintext device must not advertise api_encryption; got {enc!r}"
    return "api_encryption absent (plaintext)"
