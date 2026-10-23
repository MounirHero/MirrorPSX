// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
namespace mirror {
// Fixed 48 kHz stereo PCM16 output. The console queue is bounded to 0.5
// seconds.
class AudioQueue {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  AudioQueue();
  ~AudioQueue();
  AudioQueue(const AudioQueue &) = delete;
  AudioQueue &operator=(const AudioQueue &) = delete;
  bool open();
  bool active() const;
  void pause(bool paused);
  unsigned size() const;
  bool queue(const void *samples, unsigned bytes);
  void clear();
};
} // namespace mirror
