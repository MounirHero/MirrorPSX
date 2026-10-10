// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "imgui.h"
#include <cstdint>

namespace mirror {
enum GamepadButton : uint32_t {
    pad_confirm = 1u << 0, // DualSense Cross
    pad_cancel = 1u << 1,  // DualSense Circle
    pad_up = 1u << 2,
    pad_down = 1u << 3,
    pad_left = 1u << 4,
    pad_right = 1u << 5,
    pad_fullscreen = 1u << 6, // DualSense R1
};

struct GamepadNavigation {
    bool connected = false;
    uint32_t buttons = 0;
};

// Native PS5 hosts can feed translated scePad samples here.
// The GLFW desktop backend already sends ImGui's gamepad navigation events.
inline void submit_gamepad_navigation(ImGuiIO& io, const GamepadNavigation& pad) {
    const auto send = [&](ImGuiKey key, uint32_t bit) {
        io.AddKeyEvent(key, pad.connected && (pad.buttons & bit) != 0);
    };
    if (pad.connected) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    send(ImGuiKey_GamepadFaceDown, pad_confirm);
    send(ImGuiKey_GamepadFaceRight, pad_cancel);
    send(ImGuiKey_GamepadDpadUp, pad_up);
    send(ImGuiKey_GamepadDpadDown, pad_down);
    send(ImGuiKey_GamepadDpadLeft, pad_left);
    send(ImGuiKey_GamepadDpadRight, pad_right);
    send(ImGuiKey_GamepadR1, pad_fullscreen);
}
}
