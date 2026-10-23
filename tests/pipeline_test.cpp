// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/frame_converter.hpp"
extern "C" {
#include <libavutil/frame.h>
}
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
int main() {
  auto *input = av_frame_alloc();
  input->format = AV_PIX_FMT_YUV420P;
  input->width = input->height = 8;
  if (av_frame_get_buffer(input, 32) < 0)
    return 1;
  mirror::FrameConverter converter;
  mirror::VideoFrame frame;
  auto fill = [&](int y, int u, int v) {
    for (int n = 0; n < 8; ++n)
      std::memset(input->data[0] + n * input->linesize[0], y, 8);
    for (int n = 0; n < 4; ++n) {
      std::memset(input->data[1] + n * input->linesize[1], u, 4);
      std::memset(input->data[2] + n * input->linesize[2], v, 4);
    }
  };
  auto check = [&](int expected) {
    if (!converter.convert(*input, frame))
      return false;
    for (size_t n = 0; n < frame.rgba.size(); n += 4) {
      for (int c = 0; c < 3; ++c)
        if (std::abs(int(frame.rgba[n + c]) - expected) > 2)
          return false;
      if (frame.rgba[n + 3] != 255)
        return false;
    }
    return true;
  };
  input->color_range = AVCOL_RANGE_MPEG;
  input->colorspace = AVCOL_SPC_BT709;
  fill(16, 128, 128);
  if (!check(0))
    return 2;
  fill(235, 128, 128);
  if (!check(255))
    return 3;
  input->color_range = AVCOL_RANGE_JPEG;
  fill(0, 128, 128);
  if (!check(0))
    return 4;
  fill(255, 128, 128);
  if (!check(255))
    return 5;
  input->format = AV_PIX_FMT_YUVJ420P;
  input->color_range = AVCOL_RANGE_UNSPECIFIED;
  fill(0, 128, 128);
  if (!check(0))
    return 6;
  input->format = AV_PIX_FMT_YUV420P;
  input->color_range = AVCOL_RANGE_MPEG;
  fill(81, 90, 240);
  input->colorspace = AVCOL_SPC_SMPTE170M;
  if (!converter.convert(*input, frame))
    return 7;
  int green601 = frame.rgba[1];
  input->colorspace = AVCOL_SPC_BT709;
  if (!converter.convert(*input, frame) || int(frame.rgba[1]) - green601 < 15)
    return 8;
  av_frame_free(&input);
  mirror::State state;
  state.publish(frame);
  auto held = state.latest_frame(0);
  if (!held || held->sequence != 1)
    return 9;
  state.publish(frame);
  if (held->sequence != 1 || state.latest_frame(1)->sequence != 2)
    return 10;
  std::atomic<bool> done = false, valid = true;
  std::thread writer([&] {
    for (int n = 0; n < 500; ++n)
      state.publish(frame);
    done = true;
  });
  uint64_t sequence = 0;
  while (!done) {
    auto f = state.latest_frame(sequence);
    if (f) {
      if (f->sequence <= sequence || f->rgba.size() != 256)
        valid = false;
      sequence = f->sequence;
    }
  }
  writer.join();
  state.clear_frame();
  if (!valid || state.latest_frame(0) || state.snapshot().width ||
      !held->rgba.size())
    return 11;
  std::puts("full/limited range, YUVJ, color matrix, immutable latest-frame "
            "mailbox and concurrency: passed");
}
