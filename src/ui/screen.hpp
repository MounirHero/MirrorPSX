// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "imgui.h"
#include "receiver/state.hpp"
#include "ui/assets.hpp"
namespace mirror {
enum class Action { none, toggle_receiver, toggle_mode, toggle_media, quit };
class Screen {
  enum class Overlay { none, settings, guide, quit };
  Overlay overlay_ = Overlay::none;
  Overlay displayed_overlay_ = Overlay::none;
  float overlay_opacity_ = 0;
  int guide_device_ = 0;
  bool reduced_motion_ = false;
  bool focus_pending_ = true;
  bool was_connected_ = false;
  float hud_opacity_ = 1;
  double last_interaction_ = -1;
  UiAssets assets_;

public:
  bool initialize();
  Action draw(const Snapshot &status, ImTextureID video = 0);
};
} // namespace mirror
