// SPDX-License-Identifier: GPL-3.0-or-later
// Link-only receiver dependency smoke check, no listener or mDNS advertising.
#include "raop.h"

static void video_sink(void* cls, raop_ntp_t* ntp, video_decode_struct* frame) {
    (void)cls; (void)ntp; (void)frame;
}
static void audio_sink(void* cls, raop_ntp_t* ntp, audio_decode_struct* frame) {
    (void)cls; (void)ntp; (void)frame;
}

int main(void) {
    raop_callbacks_t callbacks = {0};
    callbacks.video_process = video_sink;
    callbacks.audio_process = audio_sink;
    raop_t* receiver = raop_init(&callbacks);
    if (!receiver) return 1;
    raop_destroy(receiver);
    return 0;
}
