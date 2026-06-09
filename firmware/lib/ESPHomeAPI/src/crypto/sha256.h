// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// Portable SHA-256 + HMAC-SHA256 + Noise-style HKDF. No dependencies.
#include <cstdint>
#include <cstring>

namespace esphome_api {
namespace crypto {

class Sha256 {
 public:
  Sha256() { reset(); }

  void reset() {
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                   0xa54ff53a, 0x510e527f, 0x9b05688c,
                                   0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h_, iv, sizeof(h_));
    len_ = 0;
    buf_len_ = 0;
  }

  void update(const uint8_t *data, size_t len) {
    len_ += len;
    while (len > 0) {
      size_t take = 64 - buf_len_;
      if (take > len) take = len;
      std::memcpy(buf_ + buf_len_, data, take);
      buf_len_ += take;
      data += take;
      len -= take;
      if (buf_len_ == 64) {
        process(buf_);
        buf_len_ = 0;
      }
    }
  }

  void finalize(uint8_t out[32]) {
    uint64_t bits = len_ * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (buf_len_ != 56) update(&zero, 1);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; i++) lenbuf[i] = (uint8_t)(bits >> (56 - 8 * i));
    update(lenbuf, 8);
    for (int i = 0; i < 8; i++) {
      out[4 * i + 0] = (uint8_t)(h_[i] >> 24);
      out[4 * i + 1] = (uint8_t)(h_[i] >> 16);
      out[4 * i + 2] = (uint8_t)(h_[i] >> 8);
      out[4 * i + 3] = (uint8_t)(h_[i]);
    }
  }

  static void hash(const uint8_t *data, size_t len, uint8_t out[32]) {
    Sha256 s;
    s.update(data, len);
    s.finalize(out);
  }

 private:
  static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void process(const uint8_t *p) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
             ((uint32_t)p[4 * i + 2] << 8) | ((uint32_t)p[4 * i + 3]);
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5],
             g = h_[6], hh = h_[7];
    for (int i = 0; i < 64; i++) {
      uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = hh + S1 + ch + k[i] + w[i];
      uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + maj;
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
  }

  uint32_t h_[8];
  uint64_t len_;
  uint8_t buf_[64];
  size_t buf_len_;
};

inline void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data,
                        size_t data_len, uint8_t out[32]) {
  uint8_t k[64];
  std::memset(k, 0, sizeof(k));
  if (key_len > 64) {
    Sha256::hash(key, key_len, k);
  } else {
    std::memcpy(k, key, key_len);
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^  0x5c;
  }
  uint8_t inner[32];
  Sha256 s;
  s.update(ipad, 64);
  s.update(data, data_len);
  s.finalize(inner);

  Sha256 o;
  o.update(opad, 64);
  o.update(inner, 32);
  o.finalize(out);
}

// Noise HKDF: derives 2 or 3 32-byte outputs from chaining key + IKM.
inline void noise_hkdf(const uint8_t ck[32], const uint8_t *ikm, size_t ikm_len,
                       int num_outputs, uint8_t *o1, uint8_t *o2, uint8_t *o3) {
  uint8_t temp_key[32];
  hmac_sha256(ck, 32, ikm, ikm_len, temp_key);

  uint8_t one = 0x01;
  hmac_sha256(temp_key, 32, &one, 1, o1);

  uint8_t buf[33];
  std::memcpy(buf, o1, 32);
  buf[32] = 0x02;
  hmac_sha256(temp_key, 32, buf, 33, o2);

  if (num_outputs == 3) {
    std::memcpy(buf, o2, 32);
    buf[32] = 0x03;
    hmac_sha256(temp_key, 32, buf, 33, o3);
  }
}

}  // namespace crypto
}  // namespace esphome_api
