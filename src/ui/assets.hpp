// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "imgui.h"
namespace mirror {
enum class Glyph {
  cross,
  circle,
  square,
  triangle,
  l1,
  r1,
  dpad,
  controller,
  screen,
  settings,
  wifi,
  pause,
  play,
  help,
  close,
  expand
};
// Artwork shares ImGui's atlas so every platform uses the same texture
// lifecycle.
class UiAssets {
  int rectangle_ = -1;
  bool loaded_ = false;

public:
  ImFont *regular = nullptr;
  ImFont *semibold = nullptr;
  ImFont *heading = nullptr;
  bool initialize();
  bool loaded() const { return loaded_; }
  void image(ImDrawList *draw, ImVec2 min, ImVec2 max, bool backdrop,
             Glyph glyph = Glyph::cross, ImU32 tint = IM_COL32_WHITE,
             float rounding = 0) const;
};
} // namespace mirror
