# SPDX-FileCopyrightText: 2026 Karl Kaiser
# SPDX-License-Identifier: AGPL-3.0-or-later
# Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

"""Minimal Protocol Buffers codec + ESPHome helpers.

Self-contained (no `protobuf` dependency). Implements only the wire-format
features the ESPHome native API uses: varint, fixed32, length-delimited,
and ZigZag sint32. Sufficient to build/parse all messages exercised by the
compliance test bed.
"""
from __future__ import annotations

import struct
from typing import Dict, List, Tuple

# ---------------------------------------------------------------------------
# VarInt
# ---------------------------------------------------------------------------


def encode_varint(value: int) -> bytes:
    """Encode an unsigned integer as a protobuf VarInt."""
    if value < 0:
        value &= (1 << 64) - 1
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def decode_varint(data: bytes, offset: int = 0) -> Tuple[int, int]:
    """Decode a VarInt at `offset`. Returns (value, new_offset)."""
    result = 0
    shift = 0
    while True:
        if offset >= len(data):
            raise ValueError("truncated varint")
        b = data[offset]
        offset += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, offset
        shift += 7
        if shift > 63:
            raise ValueError("varint too long")


# ---------------------------------------------------------------------------
# ZigZag (sint32)
# ---------------------------------------------------------------------------


def zigzag_encode32(n: int) -> int:
    return ((n << 1) ^ (n >> 31)) & 0xFFFFFFFF


def zigzag_decode32(u: int) -> int:
    return (u >> 1) ^ -(u & 1)


# ---------------------------------------------------------------------------
# Field encoders (proto3 semantics: default values are omitted)
# ---------------------------------------------------------------------------

WIRE_VARINT = 0
WIRE_FIXED64 = 1
WIRE_LEN = 2
WIRE_FIXED32 = 5


def tag(field: int, wire: int) -> bytes:
    return encode_varint((field << 3) | wire)


def enc_string(field: int, value: str, force: bool = False) -> bytes:
    raw = value.encode("utf-8")
    if not raw and not force:
        return b""
    return tag(field, WIRE_LEN) + encode_varint(len(raw)) + raw


def enc_bytes(field: int, raw: bytes, force: bool = False) -> bytes:
    if not raw and not force:
        return b""
    return tag(field, WIRE_LEN) + encode_varint(len(raw)) + raw


def enc_uint32(field: int, value: int, force: bool = False) -> bytes:
    if value == 0 and not force:
        return b""
    return tag(field, WIRE_VARINT) + encode_varint(value)


def enc_bool(field: int, value: bool, force: bool = False) -> bytes:
    if not value and not force:
        return b""
    return tag(field, WIRE_VARINT) + b"\x01"


def enc_sint32(field: int, value: int, force: bool = False) -> bytes:
    if value == 0 and not force:
        return b""
    return tag(field, WIRE_VARINT) + encode_varint(zigzag_encode32(value))


def enc_fixed32(field: int, value: int, force: bool = False) -> bytes:
    if value == 0 and not force:
        return b""
    return tag(field, WIRE_FIXED32) + struct.pack("<I", value & 0xFFFFFFFF)


def enc_float(field: int, value: float, force: bool = False) -> bytes:
    if value == 0.0 and not force:
        return b""
    return tag(field, WIRE_FIXED32) + struct.pack("<f", value)


# ---------------------------------------------------------------------------
# Generic decoder
# ---------------------------------------------------------------------------

# Decoded form: {field_number: [(wire_type, raw_value), ...]}
DecodedFields = Dict[int, List[Tuple[int, object]]]


def decode_message(data: bytes) -> DecodedFields:
    """Decode a protobuf message into a field map. Unknown fields are kept."""
    fields: DecodedFields = {}
    offset = 0
    n = len(data)
    while offset < n:
        key, offset = decode_varint(data, offset)
        field = key >> 3
        wire = key & 0x07
        if wire == WIRE_VARINT:
            val, offset = decode_varint(data, offset)
        elif wire == WIRE_FIXED64:
            val = data[offset : offset + 8]
            offset += 8
        elif wire == WIRE_LEN:
            ln, offset = decode_varint(data, offset)
            val = data[offset : offset + ln]
            offset += ln
        elif wire == WIRE_FIXED32:
            val = data[offset : offset + 4]
            offset += 4
        else:
            raise ValueError(f"unsupported wire type {wire}")
        fields.setdefault(field, []).append((wire, val))
    return fields


# Typed accessors (return proto3 defaults when field absent)


def get_string(fields: DecodedFields, f: int, default: str = "") -> str:
    if f in fields:
        return bytes(fields[f][0][1]).decode("utf-8", "replace")
    return default


def get_uint32(fields: DecodedFields, f: int, default: int = 0) -> int:
    if f in fields:
        return int(fields[f][0][1])
    return default


def get_bool(fields: DecodedFields, f: int, default: bool = False) -> bool:
    if f in fields:
        return bool(fields[f][0][1])
    return default


def get_fixed32(fields: DecodedFields, f: int, default: int = 0) -> int:
    if f in fields:
        return struct.unpack("<I", bytes(fields[f][0][1]))[0]
    return default


def get_float(fields: DecodedFields, f: int, default: float = 0.0) -> float:
    if f in fields:
        return struct.unpack("<f", bytes(fields[f][0][1]))[0]
    return default


def get_repeated_string(fields: DecodedFields, f: int) -> List[str]:
    if f not in fields:
        return []
    return [bytes(v).decode("utf-8", "replace") for _, v in fields[f]]


# ---------------------------------------------------------------------------
# FNV-1 32-bit (entity key hash)
# ---------------------------------------------------------------------------


def fnv1_hash(object_id: str) -> int:
    """ESPHome entity key = FNV-1 32-bit of the object_id."""
    h = 2166136261
    for b in object_id.encode("utf-8"):
        h = ((h * 16777619) ^ b) & 0xFFFFFFFF
    return h
