// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/screen_model.hpp"
#include "imgui.h"
namespace mirror {
// UI rendering is independent of GLFW, native pad APIs, and Vulkan surface
// creation. The host provides the ImGui Vulkan texture handle and acts on intents.
void apply_screen_style();
ScreenAction render_screen(const Snapshot& status, ImTextureID video,
                           int video_width, int video_height, ScreenUiState& ui);
}
