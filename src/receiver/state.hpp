// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace mirror {
struct VideoFrame {
  int width = 0, height = 0;
  uint64_t sequence = 0;
  std::vector<uint8_t> rgba;
};
struct Snapshot {
  bool media_mode = false, media_paused = false, media_loading = false;
  std::string media_address;
  bool listening = false, connected = false;
  std::string name = "MirrorPSX", client, pin, error;
  uint16_t port = 0;
  uint64_t frames = 0;
  int width = 0, height = 0;
};
class State {
  mutable std::mutex mutex_;
  Snapshot status_;
  std::shared_ptr<const VideoFrame> frame_;

public:
  template <class F> void update(F f) {
    std::lock_guard lock(mutex_);
    f(status_);
  }
  Snapshot snapshot() const {
    std::lock_guard lock(mutex_);
    return status_;
  }
  void publish(VideoFrame frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.width > 4096 ||
        frame.height > 4096 ||
        frame.rgba.size() != size_t(frame.width) * frame.height * 4)
      return;
    auto next = std::make_shared<VideoFrame>(std::move(frame));
    std::lock_guard lock(mutex_);
    next->sequence = ++status_.frames;
    status_.connected = true;
    status_.media_loading = false;
    status_.pin.clear();
    status_.width = next->width;
    status_.height = next->height;
    frame_ = std::move(next);
  }
  std::shared_ptr<const VideoFrame> latest_frame(uint64_t sequence) const {
    std::lock_guard lock(mutex_);
    return frame_ && frame_->sequence > sequence ? frame_ : nullptr;
  }
  bool frame_after(uint64_t sequence, VideoFrame &out) const {
    auto frame = latest_frame(sequence);
    if (!frame)
      return false;
    out = *frame;
    return true;
  }
  void clear_frame() {
    std::lock_guard lock(mutex_);
    frame_ = {};
    status_.connected = false;
    status_.media_loading = false;
    status_.width = status_.height = 0;
  }
};
} // namespace mirror
