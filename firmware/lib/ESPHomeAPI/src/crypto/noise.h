// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// Noise_NNpsk0_25519_ChaChaPoly_SHA256 — RESPONDER (device/server) side.
//
// Handshake pattern (with psk0 modifier):
//     -> psk, e
//     <- e, ee
// The device is the responder: it READS message 1 and WRITES message 2.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "chachapoly.h"
#include "random.h"
#include "sha256.h"

extern "C" {
#include "tweetnacl.h"
}

namespace esphome_api {
namespace crypto {

class NoiseResponder {
 public:
  // psk: 32-byte pre-shared key. prologue: "NoiseAPIInit" + uint16_be(len) + clienthello.
  void init(const uint8_t psk[32], const uint8_t *prologue,
            size_t prologue_len) {
    static const char *name = "Noise_NNpsk0_25519_ChaChaPoly_SHA256";  // 36 chars
    Sha256::hash(reinterpret_cast<const uint8_t *>(name), std::strlen(name), h_);
    std::memcpy(ck_, h_, 32);
    has_k_ = false;
    n_ = 0;
    std::memcpy(psk_, psk, 32);
    mix_hash(prologue, prologue_len);
  }

  // Process handshake message 1 (the Noise bytes after the 0x00 frame prefix).
  bool read_message_1(const uint8_t *msg, size_t len) {
    mix_key_and_hash(psk_, 32);          // token: psk
    if (len < 32) return false;          // token: e
    std::memcpy(re_, msg, 32);
    mix_hash(re_, 32);
    mix_key(re_, 32);                     // PSK mode: e also contributes to key
    std::vector<uint8_t> payload;        // encrypted (empty) payload
    return decrypt_and_hash(msg + 32, len - 32, payload);
  }

  // Produce handshake message 2 (Noise bytes; caller adds the 0x00 prefix).
  bool write_message_2(std::vector<uint8_t> &out) {
    random_bytes(e_priv_, 32);           // token: e
    crypto_scalarmult_base(e_pub_, e_priv_);
    out.insert(out.end(), e_pub_, e_pub_ + 32);
    mix_hash(e_pub_, 32);
    mix_key(e_pub_, 32);                  // PSK mode

    uint8_t dh[32];                       // token: ee
    crypto_scalarmult(dh, e_priv_, re_);
    mix_key(dh, 32);

    std::vector<uint8_t> ct;             // empty payload -> 16-byte tag
    encrypt_and_hash(nullptr, 0, ct);
    out.insert(out.end(), ct.begin(), ct.end());
    return true;
  }

  // Derive transport keys after the handshake.
  void split() {
    uint8_t k1[32], k2[32], unused[32];
    noise_hkdf(ck_, nullptr, 0, 2, k1, k2, unused);
    std::memcpy(k_recv_, k1, 32);  // responder receives with c1
    std::memcpy(k_send_, k2, 32);  // responder sends with c2
    n_send_ = 0;
    n_recv_ = 0;
  }

  // Transport-phase encryption (AD = empty).
  bool encrypt(const uint8_t *pt, size_t len, std::vector<uint8_t> &out) {
    uint8_t nonce[12];
    build_nonce(n_send_, nonce);
    out.resize(len + 16);
    chachapoly_encrypt(k_send_, nonce, nullptr, 0, pt, len, out.data(),
                       out.data() + len);
    n_send_++;
    return true;
  }

  bool decrypt(const uint8_t *ct, size_t len, std::vector<uint8_t> &out) {
    if (len < 16) return false;
    uint8_t nonce[12];
    build_nonce(n_recv_, nonce);
    out.resize(len - 16);
    bool ok = chachapoly_decrypt(k_recv_, nonce, nullptr, 0, ct, len - 16,
                                 ct + len - 16, out.data());
    if (ok) n_recv_++;
    return ok;
  }

 private:
  static void build_nonce(uint64_t counter, uint8_t out[12]) {
    out[0] = out[1] = out[2] = out[3] = 0;  // 32 bits of zeros
    for (int i = 0; i < 8; i++) out[4 + i] = (uint8_t)(counter >> (8 * i));
  }

  void mix_hash(const uint8_t *data, size_t len) {
    Sha256 s;
    s.update(h_, 32);
    if (len) s.update(data, len);
    s.finalize(h_);
  }

  void mix_key(const uint8_t *input, size_t len) {
    uint8_t new_ck[32], temp_k[32], unused[32];
    noise_hkdf(ck_, input, len, 2, new_ck, temp_k, unused);
    std::memcpy(ck_, new_ck, 32);
    std::memcpy(k_, temp_k, 32);
    n_ = 0;
    has_k_ = true;
  }

  void mix_key_and_hash(const uint8_t *input, size_t len) {
    uint8_t new_ck[32], temp_h[32], temp_k[32];
    noise_hkdf(ck_, input, len, 3, new_ck, temp_h, temp_k);
    std::memcpy(ck_, new_ck, 32);
    mix_hash(temp_h, 32);
    std::memcpy(k_, temp_k, 32);
    n_ = 0;
    has_k_ = true;
  }

  bool encrypt_and_hash(const uint8_t *pt, size_t len,
                        std::vector<uint8_t> &out) {
    if (has_k_) {
      uint8_t nonce[12];
      build_nonce(n_, nonce);
      out.resize(len + 16);
      chachapoly_encrypt(k_, nonce, h_, 32, pt, len, out.data(),
                         out.data() + len);
      n_++;
    } else {
      out.assign(pt, pt + len);
    }
    mix_hash(out.data(), out.size());
    return true;
  }

  bool decrypt_and_hash(const uint8_t *ct, size_t len,
                        std::vector<uint8_t> &out) {
    if (has_k_) {
      if (len < 16) return false;
      uint8_t nonce[12];
      build_nonce(n_, nonce);
      out.resize(len - 16);
      bool ok = chachapoly_decrypt(k_, nonce, h_, 32, ct, len - 16,
                                   ct + len - 16, out.data());
      if (!ok) return false;
      n_++;
    } else {
      out.assign(ct, ct + len);
    }
    mix_hash(ct, len);
    return true;
  }

  uint8_t h_[32];
  uint8_t ck_[32];
  uint8_t k_[32];
  uint64_t n_ = 0;
  bool has_k_ = false;

  uint8_t psk_[32];
  uint8_t e_priv_[32];
  uint8_t e_pub_[32];
  uint8_t re_[32];

  uint8_t k_send_[32];
  uint8_t k_recv_[32];
  uint64_t n_send_ = 0;
  uint64_t n_recv_ = 0;
};

}  // namespace crypto
}  // namespace esphome_api
