// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Provides the randombytes() symbol TweetNaCl requires.
#include "random.h"

extern "C" {
#include "tweetnacl.h"
}

extern "C" void randombytes(unsigned char *x, unsigned long long n) {
  esphome_api::crypto::random_bytes(x, (size_t)n);
}
