// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// Minimal, portable Protocol Buffers codec for the ESPHome native API.
// No external dependencies (no nanopb / protoc). proto3 semantics: default
// scalar values are omitted from the wire unless `force` is set.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace esphome_api {

enum WireType : uint8_t {
  WIRE_VARINT = 0,
  WIRE_FIXED64 = 1,
  WIRE_LEN = 2,
  WIRE_FIXED32 = 5,
};

// --------------------------------------------------------------------------
// Writer
// --------------------------------------------------------------------------
class ProtoWriteBuffer {
 public:
  std::vector<uint8_t> data;

  void clear() { data.clear(); }
  size_t size() const { return data.size(); }

  void write_byte(uint8_t b) { data.push_back(b); }

  void write_varint(uint64_t value) {
    while (true) {
      uint8_t byte = value & 0x7F;
      value >>= 7;
      if (value != 0) {
        data.push_back(byte | 0x80);
      } else {
        data.push_back(byte);
        break;
      }
    }
  }

  void write_tag(uint32_t field, WireType wire) {
    write_varint((static_cast<uint64_t>(field) << 3) | wire);
  }

  void encode_uint32(uint32_t field, uint32_t value, bool force = false) {
    if (value == 0 && !force) return;
    write_tag(field, WIRE_VARINT);
    write_varint(value);
  }

  void encode_int32(uint32_t field, int32_t value, bool force = false) {
    if (value == 0 && !force) return;
    write_tag(field, WIRE_VARINT);
    // Negative int32 is sign-extended to 64 bits on the wire (10 bytes).
    write_varint(static_cast<uint64_t>(static_cast<int64_t>(value)));
  }

  void encode_sint32(uint32_t field, int32_t value, bool force = false) {
    if (value == 0 && !force) return;
    write_tag(field, WIRE_VARINT);
    uint32_t zz = (static_cast<uint32_t>(value) << 1) ^
                  static_cast<uint32_t>(value >> 31);
    write_varint(zz);
  }

  void encode_bool(uint32_t field, bool value, bool force = false) {
    if (!value && !force) return;
    write_tag(field, WIRE_VARINT);
    write_varint(value ? 1 : 0);
  }

  void encode_enum(uint32_t field, uint32_t value, bool force = false) {
    encode_uint32(field, value, force);
  }

  void encode_string(uint32_t field, const std::string &value,
                     bool force = false) {
    if (value.empty() && !force) return;
    write_tag(field, WIRE_LEN);
    write_varint(value.size());
    data.insert(data.end(), value.begin(), value.end());
  }

  void encode_repeated_string(uint32_t field,
                              const std::vector<std::string> &values) {
    for (const auto &v : values) encode_string(field, v, true);
  }

  void encode_repeated_uint32(uint32_t field,
                              const std::vector<uint32_t> &values) {
    for (uint32_t v : values) encode_uint32(field, v, true);
  }

  void encode_bytes(uint32_t field, const uint8_t *ptr, size_t len,
                    bool force = false) {
    if (len == 0 && !force) return;
    write_tag(field, WIRE_LEN);
    write_varint(len);
    data.insert(data.end(), ptr, ptr + len);
  }

  void encode_fixed32(uint32_t field, uint32_t value, bool force = false) {
    if (value == 0 && !force) return;
    write_tag(field, WIRE_FIXED32);
    data.push_back(value & 0xFF);
    data.push_back((value >> 8) & 0xFF);
    data.push_back((value >> 16) & 0xFF);
    data.push_back((value >> 24) & 0xFF);
  }

  void encode_float(uint32_t field, float value, bool force = false) {
    if (value == 0.0f && !force) return;
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    encode_fixed32(field, bits, true);
  }
};

// Append a VarInt to an arbitrary byte vector (used for frame headers).
inline void append_varint(std::vector<uint8_t> &out, uint64_t value) {
  while (true) {
    uint8_t byte = value & 0x7F;
    value >>= 7;
    if (value != 0) {
      out.push_back(byte | 0x80);
    } else {
      out.push_back(byte);
      break;
    }
  }
}

// --------------------------------------------------------------------------
// Reader
// --------------------------------------------------------------------------
struct ProtoField {
  uint32_t field = 0;
  uint32_t wire = 0;
  uint64_t as_varint = 0;       // valid when wire == WIRE_VARINT
  uint32_t as_fixed32 = 0;      // valid when wire == WIRE_FIXED32
  uint64_t as_fixed64 = 0;      // valid when wire == WIRE_FIXED64
  const uint8_t *as_bytes = nullptr;  // valid when wire == WIRE_LEN
  size_t bytes_len = 0;

  bool as_bool() const { return as_varint != 0; }
  std::string as_string() const {
    return std::string(reinterpret_cast<const char *>(as_bytes), bytes_len);
  }
  float as_float() const {
    float f;
    std::memcpy(&f, &as_fixed32, sizeof(f));
    return f;
  }
};

class ProtoDecoder {
 public:
  ProtoDecoder(const uint8_t *data, size_t len) : d_(data), n_(len) {}

  bool next(ProtoField &out) {
    if (pos_ >= n_) return false;
    uint64_t key;
    if (!read_varint(key)) return false;
    out.field = static_cast<uint32_t>(key >> 3);
    out.wire = static_cast<uint32_t>(key & 0x07);
    out.as_varint = 0;
    out.as_fixed32 = 0;
    out.as_fixed64 = 0;
    out.as_bytes = nullptr;
    out.bytes_len = 0;
    switch (out.wire) {
      case WIRE_VARINT:
        return read_varint(out.as_varint);
      case WIRE_FIXED32: {
        if (pos_ + 4 > n_) return false;
        out.as_fixed32 = static_cast<uint32_t>(d_[pos_]) |
                         (static_cast<uint32_t>(d_[pos_ + 1]) << 8) |
                         (static_cast<uint32_t>(d_[pos_ + 2]) << 16) |
                         (static_cast<uint32_t>(d_[pos_ + 3]) << 24);
        pos_ += 4;
        return true;
      }
      case WIRE_FIXED64: {
        if (pos_ + 8 > n_) return false;
        out.as_fixed64 = 0;
        for (int i = 0; i < 8; i++)
          out.as_fixed64 |= static_cast<uint64_t>(d_[pos_ + i]) << (8 * i);
        pos_ += 8;
        return true;
      }
      case WIRE_LEN: {
        uint64_t len;
        if (!read_varint(len)) return false;
        if (pos_ + len > n_) return false;
        out.as_bytes = d_ + pos_;
        out.bytes_len = static_cast<size_t>(len);
        pos_ += static_cast<size_t>(len);
        return true;
      }
      default:
        return false;  // unsupported wire type
    }
  }

 private:
  bool read_varint(uint64_t &out) {
    out = 0;
    int shift = 0;
    while (pos_ < n_) {
      uint8_t b = d_[pos_++];
      out |= static_cast<uint64_t>(b & 0x7F) << shift;
      if (!(b & 0x80)) return true;
      shift += 7;
      if (shift > 63) return false;
    }
    return false;
  }

  const uint8_t *d_;
  size_t n_;
  size_t pos_ = 0;
};

// Parse a streaming VarInt. Returns false if the buffer is too short
// (caller should wait for more bytes). On success advances `pos`.
inline bool stream_varint(const uint8_t *data, size_t len, size_t &pos,
                          uint32_t &out) {
  uint32_t result = 0;
  int shift = 0;
  size_t p = pos;
  while (p < len) {
    uint8_t b = data[p++];
    result |= static_cast<uint32_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) {
      out = result;
      pos = p;
      return true;
    }
    shift += 7;
    if (shift > 31) return false;  // malformed
  }
  return false;  // incomplete
}

}  // namespace esphome_api
