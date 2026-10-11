// SPDX-License-Identifier: GPL-3.0-or-later
// Ensure the PS5's no-keyfile mode cannot derive the same private key from
// the public, stable device ID on every invocation.
#include "pairing.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    int generated_a = -1, generated_b = -1;
    pairing_t* first = pairing_init_generate("02:4D:50:53:58:01", "", &generated_a);
    pairing_t* second = pairing_init_generate("02:4D:50:53:58:01", "", &generated_b);
    if (!first || !second) {
        fprintf(stderr, "Ephemeral Ed25519 generation failed\n");
        if (first) pairing_destroy(first);
        if (second) pairing_destroy(second);
        return 1;
    }
    unsigned char one[ED25519_KEY_SIZE] = {0};
    unsigned char two[ED25519_KEY_SIZE] = {0};
    pairing_get_public_key(first, one);
    pairing_get_public_key(second, two);
    pairing_destroy(first);
    pairing_destroy(second);
    if (generated_a != 0 || generated_b != 0 || memcmp(one, two, sizeof(one)) == 0) {
        fprintf(stderr, "Ephemeral identity was unexpectedly reused or persisted\n");
        return 1;
    }
    return 0;
}
