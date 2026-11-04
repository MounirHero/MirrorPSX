// SPDX-License-Identifier: GPL-3.0-or-later
// Linked only into pin_short_harness. Force a reproducible SRP encoding case:
// salt=1 and PIN=0228 produce a 255-byte verifier for the integration username.
// Production binaries keep OpenSSL entropy and random PIN generation unchanged.
#include <openssl/bn.h>

int __real_BN_rand(BIGNUM *, int, int, int);

int __wrap_BN_rand(BIGNUM *value, int bits, int top, int bottom) {
    if (bits == 128) return BN_set_word(value, 1);
    return __real_BN_rand(value, bits, top, bottom);
}

int __wrap_random_pin(void) { return 228; }
