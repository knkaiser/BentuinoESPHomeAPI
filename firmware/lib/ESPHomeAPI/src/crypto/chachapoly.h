// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// Portable ChaCha20-Poly1305 AEAD (RFC 8439 / IETF, 96-bit nonce). No deps.
#include <cstdint>
#include <cstring>

namespace esphome_api {
namespace crypto {

// ---------------------------------------------------------------- ChaCha20
inline uint32_t cc_rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

inline void chacha20_block(const uint32_t in[16], uint8_t out[64]) {
  uint32_t x[16];
  std::memcpy(x, in, sizeof(x));
  for (int i = 0; i < 10; i++) {
#define QR(a, b, c, d)                       \
  x[a] += x[b]; x[d] ^= x[a]; x[d] = cc_rotl(x[d], 16); \
  x[c] += x[d]; x[b] ^= x[c]; x[b] = cc_rotl(x[b], 12); \
  x[a] += x[b]; x[d] ^= x[a]; x[d] = cc_rotl(x[d], 8);  \
  x[c] += x[d]; x[b] ^= x[c]; x[b] = cc_rotl(x[b], 7);
    QR(0, 4, 8, 12) QR(1, 5, 9, 13) QR(2, 6, 10, 14) QR(3, 7, 11, 15)
    QR(0, 5, 10, 15) QR(1, 6, 11, 12) QR(2, 7, 8, 13) QR(3, 4, 9, 14)
#undef QR
  }
  for (int i = 0; i < 16; i++) {
    uint32_t v = x[i] + in[i];
    out[4 * i + 0] = (uint8_t)(v);
    out[4 * i + 1] = (uint8_t)(v >> 8);
    out[4 * i + 2] = (uint8_t)(v >> 16);
    out[4 * i + 3] = (uint8_t)(v >> 24);
  }
}

inline void chacha20_init(uint32_t st[16], const uint8_t key[32],
                          const uint8_t nonce[12], uint32_t counter) {
  st[0] = 0x61707865; st[1] = 0x3320646e;
  st[2] = 0x79622d32; st[3] = 0x6b206574;
  for (int i = 0; i < 8; i++)
    st[4 + i] = (uint32_t)key[4 * i] | ((uint32_t)key[4 * i + 1] << 8) |
                ((uint32_t)key[4 * i + 2] << 16) |
                ((uint32_t)key[4 * i + 3] << 24);
  st[12] = counter;
  for (int i = 0; i < 3; i++)
    st[13 + i] = (uint32_t)nonce[4 * i] | ((uint32_t)nonce[4 * i + 1] << 8) |
                 ((uint32_t)nonce[4 * i + 2] << 16) |
                 ((uint32_t)nonce[4 * i + 3] << 24);
}

inline void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                         uint32_t counter, const uint8_t *in, uint8_t *out,
                         size_t len) {
  uint32_t st[16];
  chacha20_init(st, key, nonce, counter);
  uint8_t ks[64];
  size_t off = 0;
  while (off < len) {
    chacha20_block(st, ks);
    st[12]++;
    size_t n = len - off;
    if (n > 64) n = 64;
    for (size_t i = 0; i < n; i++) out[off + i] = in[off + i] ^ ks[i];
    off += n;
  }
}

// ---------------------------------------------------------------- Poly1305
class Poly1305 {
 public:
  Poly1305(const uint8_t key[32]) {
    r_[0] = (u32(key) >> 0) & 0x3ffffff;
    r_[1] = (u32(key + 3) >> 2) & 0x3ffff03;
    r_[2] = (u32(key + 6) >> 4) & 0x3ffc0ff;
    r_[3] = (u32(key + 9) >> 6) & 0x3f03fff;
    r_[4] = (u32(key + 12) >> 8) & 0x00fffff;
    for (int i = 0; i < 5; i++) h_[i] = 0;
    pad_[0] = u32(key + 16); pad_[1] = u32(key + 20);
    pad_[2] = u32(key + 24); pad_[3] = u32(key + 28);
    leftover_ = 0; final_ = 0;
  }

  void update(const uint8_t *m, size_t bytes) {
    if (leftover_) {
      size_t want = 16 - leftover_;
      if (want > bytes) want = bytes;
      for (size_t i = 0; i < want; i++) buffer_[leftover_ + i] = m[i];
      bytes -= want; m += want; leftover_ += want;
      if (leftover_ < 16) return;
      block(buffer_, false);
      leftover_ = 0;
    }
    while (bytes >= 16) {
      block(m, false);
      m += 16; bytes -= 16;
    }
    for (size_t i = 0; i < bytes; i++) buffer_[leftover_ + i] = m[i];
    leftover_ += bytes;
  }

  void finish(uint8_t mac[16]) {
    if (leftover_) {
      buffer_[leftover_++] = 1;
      for (size_t i = leftover_; i < 16; i++) buffer_[i] = 0;
      final_ = 1;
      block(buffer_, true);
    }
    uint64_t f;
    uint32_t g[5];
    f = (uint64_t)h_[0] + 5; g[0] = (uint32_t)(f & 0x3ffffff); f >>= 26;
    f += h_[1]; g[1] = (uint32_t)(f & 0x3ffffff); f >>= 26;
    f += h_[2]; g[2] = (uint32_t)(f & 0x3ffffff); f >>= 26;
    f += h_[3]; g[3] = (uint32_t)(f & 0x3ffffff); f >>= 26;
    f += h_[4] - (1UL << 26); g[4] = (uint32_t)f;
    uint32_t mask = (g[4] >> 31) - 1;
    for (int i = 0; i < 5; i++) g[i] &= mask;
    mask = ~mask;
    for (int i = 0; i < 5; i++) h_[i] = (h_[i] & mask) | g[i];

    uint64_t h0 = h_[0] | (h_[1] << 26);
    uint64_t h1 = (h_[1] >> 6) | (h_[2] << 20);
    uint64_t h2 = (h_[2] >> 12) | (h_[3] << 14);
    uint64_t h3 = (h_[3] >> 18) | (h_[4] << 8);

    uint64_t t = (uint64_t)h0 + pad_[0]; h0 = (uint32_t)t; t >>= 32;
    t += (uint64_t)h1 + pad_[1]; h1 = (uint32_t)t; t >>= 32;
    t += (uint64_t)h2 + pad_[2]; h2 = (uint32_t)t; t >>= 32;
    t += (uint64_t)h3 + pad_[3]; h3 = (uint32_t)t;

    put32(mac + 0, (uint32_t)h0); put32(mac + 4, (uint32_t)h1);
    put32(mac + 8, (uint32_t)h2); put32(mac + 12, (uint32_t)h3);
  }

 private:
  static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
  }
  static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
  }

  void block(const uint8_t *m, bool is_final) {
    const uint32_t hibit = is_final ? 0 : (1UL << 24);
    uint32_t r0 = r_[0], r1 = r_[1], r2 = r_[2], r3 = r_[3], r4 = r_[4];
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = h_[0], h1 = h_[1], h2 = h_[2], h3 = h_[3], h4 = h_[4];

    h0 += (u32(m + 0) >> 0) & 0x3ffffff;
    h1 += (u32(m + 3) >> 2) & 0x3ffffff;
    h2 += (u32(m + 6) >> 4) & 0x3ffffff;
    h3 += (u32(m + 9) >> 6) & 0x3ffffff;
    h4 += (u32(m + 12) >> 8) | hibit;

    uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 +
                  (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
    uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 +
                  (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
    uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 +
                  (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
    uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 +
                  (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
    uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 +
                  (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

    uint32_t c;
    c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff; d1 += c;
    c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff; d2 += c;
    c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff; d3 += c;
    c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff; d4 += c;
    c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 = h0 & 0x3ffffff; h1 += c;

    h_[0] = h0; h_[1] = h1; h_[2] = h2; h_[3] = h3; h_[4] = h4;
  }

  uint32_t r_[5];
  uint32_t h_[5];
  uint32_t pad_[4];
  uint8_t buffer_[16];
  size_t leftover_;
  int final_;
};

// --------------------------------------------------- AEAD (RFC 8439)
inline void poly1305_key_gen(const uint8_t key[32], const uint8_t nonce[12],
                             uint8_t out[32]) {
  uint32_t st[16];
  chacha20_init(st, key, nonce, 0);
  uint8_t block[64];
  chacha20_block(st, block);
  std::memcpy(out, block, 32);
}

inline void aead_tag(const uint8_t poly_key[32], const uint8_t *ad,
                     size_t ad_len, const uint8_t *ct, size_t ct_len,
                     uint8_t tag[16]) {
  Poly1305 p(poly_key);
  static const uint8_t zeros[16] = {0};
  p.update(ad, ad_len);
  if (ad_len % 16) p.update(zeros, 16 - (ad_len % 16));
  p.update(ct, ct_len);
  if (ct_len % 16) p.update(zeros, 16 - (ct_len % 16));
  uint8_t lengths[16];
  uint64_t a = ad_len, c = ct_len;
  for (int i = 0; i < 8; i++) lengths[i] = (uint8_t)(a >> (8 * i));
  for (int i = 0; i < 8; i++) lengths[8 + i] = (uint8_t)(c >> (8 * i));
  p.update(lengths, 16);
  p.finish(tag);
}

// Encrypt: out must hold pt_len bytes; tag (16) returned separately.
inline void chachapoly_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                               const uint8_t *ad, size_t ad_len,
                               const uint8_t *pt, size_t pt_len, uint8_t *out,
                               uint8_t tag[16]) {
  uint8_t poly_key[32];
  poly1305_key_gen(key, nonce, poly_key);
  if (pt_len) chacha20_xor(key, nonce, 1, pt, out, pt_len);
  aead_tag(poly_key, ad, ad_len, out, pt_len, tag);
}

// Decrypt: verifies tag. Returns true on success; out holds ct_len bytes.
inline bool chachapoly_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                               const uint8_t *ad, size_t ad_len,
                               const uint8_t *ct, size_t ct_len,
                               const uint8_t tag[16], uint8_t *out) {
  uint8_t poly_key[32];
  poly1305_key_gen(key, nonce, poly_key);
  uint8_t expect[16];
  aead_tag(poly_key, ad, ad_len, ct, ct_len, expect);
  uint8_t diff = 0;
  for (int i = 0; i < 16; i++) diff |= expect[i] ^ tag[i];
  if (diff != 0) return false;
  if (ct_len) chacha20_xor(key, nonce, 1, ct, out, ct_len);
  return true;
}

}  // namespace crypto
}  // namespace esphome_api
