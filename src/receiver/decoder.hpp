// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/frame_converter.hpp"
#include "receiver/state.hpp"
struct AVCodecContext;
struct AVFrame;
namespace mirror {
class Decoder {
  AVCodecContext *context_ = nullptr;
  AVFrame *frame_ = nullptr;
  FrameConverter converter_;
  State &state_;
  bool receive_frames();

public:
  Decoder(const Decoder &) = delete;
  Decoder &operator=(const Decoder &) = delete;
  explicit Decoder(State &state) : state_(state) {}
  ~Decoder();
  bool open(bool hevc = false);
  bool push(const uint8_t *data, int size);
  void flush();
};
} // namespace mirror
