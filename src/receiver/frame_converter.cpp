// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/frame_converter.hpp"
extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}
namespace mirror {
FrameConverter::~FrameConverter() { sws_freeContext(scale_); }
bool FrameConverter::convert(const AVFrame &s, VideoFrame &out) {
  if (s.width <= 0 || s.height <= 0 || s.width > 4096 || s.height > 4096)
    return false;
  auto format = static_cast<AVPixelFormat>(s.format);
  bool full = s.color_range == AVCOL_RANGE_JPEG;
  switch (format) {
  case AV_PIX_FMT_YUVJ420P:
    format = AV_PIX_FMT_YUV420P;
    full = true;
    break;
  case AV_PIX_FMT_YUVJ422P:
    format = AV_PIX_FMT_YUV422P;
    full = true;
    break;
  case AV_PIX_FMT_YUVJ444P:
    format = AV_PIX_FMT_YUV444P;
    full = true;
    break;
  case AV_PIX_FMT_YUVJ440P:
    format = AV_PIX_FMT_YUV440P;
    full = true;
    break;
  default:
    break;
  }
  scale_ = sws_getCachedContext(scale_, s.width, s.height, format, s.width,
                                s.height, AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                nullptr, nullptr, nullptr);
  if (!scale_)
    return false;
  int colorspace = SWS_CS_DEFAULT;
  if (s.colorspace == AVCOL_SPC_BT709)
    colorspace = SWS_CS_ITU709;
  else if (s.colorspace == AVCOL_SPC_BT2020_NCL)
    colorspace = SWS_CS_BT2020;
  else if (s.colorspace == AVCOL_SPC_SMPTE240M)
    colorspace = SWS_CS_SMPTE240M;
  const auto *coeff = sws_getCoefficients(colorspace);
  if (sws_setColorspaceDetails(scale_, coeff, full, coeff, 1, 0, 1 << 16,
                               1 << 16) < 0)
    return false;
  out.width = s.width;
  out.height = s.height;
  const auto bytes = size_t(s.width) * s.height * 4;
  // swscale's SIMD implementations may write beyond the final packed pixel.
  // Keep padding allocated during conversion; expose only the logical image.
  out.rgba.resize(bytes + 64);
  uint8_t *dst[] = {out.rgba.data(), nullptr, nullptr, nullptr};
  int stride[] = {s.width * 4, 0, 0, 0};
  bool valid = sws_scale(scale_, s.data, s.linesize, 0, s.height, dst,
                         stride) == s.height;
  out.rgba.resize(bytes);
  return valid;
}
} // namespace mirror
