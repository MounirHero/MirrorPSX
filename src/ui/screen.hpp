// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/screen_model.hpp"
#include "imgui.h"
namespace mirror {
// UI rendering is independent of GLFW, native pad APIs, and Vulkan surface
// creation. The host provides the ImGui Vulkan texture handle and acts on intents.
// Fonts are supplied by each host. A native console can use a bundled,
// appropriately licensed sans-serif without introducing a platform dependency.
void apply_screen_style(ImFont* regular = nullptr, ImFont* semibold = nullptr,
                        ImFont* icon_font = nullptr);
ScreenAction render_screen(const Snapshot& status, ImTextureID video,
                           int video_width, int video_height, ScreenUiState& ui);
}
