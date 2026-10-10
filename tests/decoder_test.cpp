// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/decoder.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}
#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    mirror::State state;
    mirror::Decoder decoder(state);
    assert(decoder.open());
    assert(!decoder.push(nullptr, 100));
    const uint8_t invalid[] = {0, 0, 0, 1, 0xFF};
    assert(!decoder.push(invalid, -1));
    assert(!decoder.push(invalid, 16 * 1024 * 1024 + 1));
    decoder.flush();
    assert(state.snapshot().width == 0);
    assert(decoder.open(true));
    decoder.flush();
    assert(decoder.open());

    // Exercise actual decoding when the FFmpeg build includes an H.264 encoder.
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder) return 0;
    AVCodecContext* ctx = avcodec_alloc_context3(encoder);
    assert(ctx);
    ctx->width = 64;
    ctx->height = 64;
    ctx->time_base = AVRational{1, 30};
    ctx->framerate = AVRational{30, 1};
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->gop_size = 1;
    ctx->max_b_frames = 0;
    av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
    if (avcodec_open2(ctx, encoder, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return 0;
    }
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    assert(frame && packet);
    frame->width = 64;
    frame->height = 64;
    frame->format = AV_PIX_FMT_YUV420P;
    assert(av_frame_get_buffer(frame, 32) >= 0);
    assert(av_frame_make_writable(frame) >= 0);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) frame->data[0][y * frame->linesize[0] + x] = 80;
    }
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            frame->data[1][y * frame->linesize[1] + x] = 128;
            frame->data[2][y * frame->linesize[2] + x] = 128;
        }
    }
    for (int i=0; i<5; ++i) {
        frame->pts = i;
        assert(avcodec_send_frame(ctx, frame) >= 0);
        while (avcodec_receive_packet(ctx, packet) >= 0) {
            assert(decoder.push(packet->data, packet->size));
            av_packet_unref(packet);
        }
    }
    // Encoders may buffer input frames until draining begins.
    assert(avcodec_send_frame(ctx, nullptr) >= 0);
    while (avcodec_receive_packet(ctx, packet) >= 0) {
        assert(decoder.push(packet->data, packet->size));
        av_packet_unref(packet);
    }
    mirror::VideoFrame decoded;
    assert(state.frame_after(0, decoded));
    assert(decoded.width == 64 && decoded.height == 64);
    assert(decoded.rgba.size() == 64 * 64 * 4);
    decoder.flush();
    assert(state.snapshot().width == 0 && state.snapshot().height == 0);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
}
