// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/media_player.hpp"
#include <memory>
namespace mirror {
class DlnaReceiver {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  DlnaReceiver(State &state, MediaPlayer &player);
  ~DlnaReceiver();
  bool start();
  void stop();
};
} // namespace mirror
