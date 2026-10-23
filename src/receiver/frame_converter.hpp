// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/state.hpp"
struct AVFrame;
struct SwsContext;
namespace mirror {
class FrameConverter {
  SwsContext *scale_ = nullptr;

public:
  FrameConverter() = default;
  FrameConverter(const FrameConverter &) = delete;
  FrameConverter &operator=(const FrameConverter &) = delete;
  ~FrameConverter();
  bool convert(const AVFrame &source, VideoFrame &output);
};
} // namespace mirror
