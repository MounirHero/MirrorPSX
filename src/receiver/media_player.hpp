// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/state.hpp"
#include <memory>
namespace mirror {
struct MediaStatus {
  std::string uri, title, transport = "NO_MEDIA_PRESENT", error;
  double position = 0, duration = 0;
  int volume = 100;
  bool muted = false;
};
class MediaPlayer {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  explicit MediaPlayer(State &state);
  ~MediaPlayer();
  bool load(const std::string &uri, const std::string &title);
  void play();
  void pause();
  void stop();
  bool seek(double seconds);
  void volume(int value);
  void mute(bool value);
  MediaStatus status() const;
};
} // namespace mirror
