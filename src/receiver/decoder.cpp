// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/decoder.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#include <cstring>
namespace mirror {
Decoder::~Decoder() {
  avcodec_free_context(&context_);
  av_frame_free(&frame_);
}
bool Decoder::open(bool hevc) {
  avcodec_free_context(&context_);
  av_frame_free(&frame_);
  const AVCodec *codec =
      avcodec_find_decoder(hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
  if (!codec)
    return false;
  context_ = avcodec_alloc_context3(codec);
  frame_ = av_frame_alloc();
  if (!context_ || !frame_) {
    avcodec_free_context(&context_);
    av_frame_free(&frame_);
    return false;
  }
  context_->thread_count = 2;
  // Frame threading buffers future frames. Mirroring prioritizes latency.
  context_->thread_type = FF_THREAD_SLICE;
  context_->max_pixels = 4096LL * 4096;
  if (avcodec_open2(context_, codec, nullptr) < 0) {
    avcodec_free_context(&context_);
    av_frame_free(&frame_);
    return false;
  }
  return true;
}
bool Decoder::receive_frames() {
  int result = 0;
  while ((result = avcodec_receive_frame(context_, frame_)) >= 0) {
    VideoFrame output;
    if (!converter_.convert(*frame_, output)) {
      av_frame_unref(frame_);
      return false;
    }
    state_.publish(std::move(output));
    av_frame_unref(frame_);
  }
  return result == AVERROR(EAGAIN) || result == AVERROR_EOF;
}
void Decoder::flush() {
  if (context_)
    avcodec_flush_buffers(context_);
  if (frame_)
    av_frame_unref(frame_);
  state_.clear_frame();
}
bool Decoder::push(const uint8_t *data, int size) {
  if (!context_ || !frame_ || !data || size <= 0 || size > 16 * 1024 * 1024)
    return false;
  AVPacket *packet = av_packet_alloc();
  if (!packet)
    return false;
  // av_new_packet gives FFmpeg its required input padding (unlike wrapping
  // the network buffer directly).
  if (av_new_packet(packet, size) < 0) {
    av_packet_free(&packet);
    return false;
  }
  std::memcpy(packet->data, data, size);
  int result = avcodec_send_packet(context_, packet);
  if (result == AVERROR(EAGAIN)) {
    // The decoder still owns an unconsumed frame. Drain it and retry the
    // same packet; accepting EAGAIN as success silently lost video data.
    if (!receive_frames()) {
      av_packet_free(&packet);
      return false;
    }
    result = avcodec_send_packet(context_, packet);
  }
  av_packet_free(&packet);
  if (result < 0)
    return false;
  return receive_frames();
}
} // namespace mirror
