// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real UxPlay allocator/HTTP socket teardown without mDNS,
// AirPlay clients, persistent identity keys, or PS5 services.
#include "raop.h"
#include <stdio.h>

static void video_process(void* context, raop_ntp_t* clock, video_decode_struct* frame) {
    (void)context; (void)clock; (void)frame;
}
static void audio_process(void* context, raop_ntp_t* clock, audio_decode_struct* frame) {
    (void)context; (void)clock; (void)frame;
}

int main(void) {
    raop_callbacks_t incomplete = {0};
    if (raop_init(&incomplete)) {
        fprintf(stderr, "RAOP accepted missing audio/video callbacks\n");
        return 1;
    }
    raop_callbacks_t callbacks = {0};
    callbacks.video_process = video_process;
    callbacks.audio_process = audio_process;
    for (int n = 0; n < 8; ++n) {
        raop_t* server = raop_init(&callbacks);
        if (!server) return 2;
        if ((n % 2) == 0) {
            // Prior to initializing httpd, destroy must not dereference it.
            raop_destroy(server);
            continue;
        }
        if (raop_init2(server, 0, "02:4D:50:53:58:FE", "") != 0) return 3;
        if (raop_init2(server, 0, "02:4D:50:53:58:FE", "") == 0) {
            fprintf(stderr, "RAOP accepted repeated key/HTTP initialization\n");
            return 4;
        }
        unsigned short port = 0;
        if (raop_start_httpd(server, &port) < 0 || port == 0) return 5;
        raop_stop_httpd(server);
        raop_stop_httpd(server);
        raop_destroy(server);
    }
    return 0;
}
