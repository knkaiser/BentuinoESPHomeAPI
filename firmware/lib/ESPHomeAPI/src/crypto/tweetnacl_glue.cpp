// Provides the randombytes() symbol TweetNaCl requires.
#include "random.h"

extern "C" {
#include "tweetnacl.h"
}

extern "C" void randombytes(unsigned char *x, unsigned long long n) {
  esphome_api::crypto::random_bytes(x, (size_t)n);
}
