// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/state.hpp"
#include <algorithm>
#include <cstdint>

namespace mirror {
// This model is independent of the window system, Vulkan, ImGui and PS5 SDK.
enum class ScreenPhase { stopped, error, waiting, connected, pairing, streaming };
enum class ScreenAction { none, start, stop };

struct ScreenUiState {
    bool fullscreen = false;
    bool focus_primary = true;
    bool focus_fullscreen = false;
    bool confirm_stop = false;
    bool help_open = false;
    bool reduced_motion = false;
    float primary_glow = 0.0f;
    float help_glow = 0.0f;
    float fullscreen_glow = 0.0f;
};

struct ScreenModel {
    ScreenPhase phase = ScreenPhase::stopped;
    bool has_video = false;
    bool allow_fullscreen = false;
    bool can_start = true;
    uint64_t frames = 0;
    int width = 0;
    int height = 0;
    const char* status_label = "OFFLINE";
    const char* headline = "Receiver is offline";
    const char* detail = "Start the receiver to make MirrorPSX discoverable.";
};

inline ScreenModel make_screen_model(const Snapshot& status, bool uploaded_video) {
    ScreenModel model{};
    model.frames = status.frames;
    model.width = status.width;
    model.height = status.height;
    model.has_video = status.listening && uploaded_video && status.width > 0 && status.height > 0;
    // A pairing request must have priority over video/fullscreen controls.
    model.allow_fullscreen = model.has_video && status.pin.empty();
    model.can_start = !status.listening;

    if (!status.listening && !status.error.empty()) {
        model.phase = ScreenPhase::error;
        model.status_label = "NEEDS ATTENTION";
        model.headline = "Receiver couldn't start";
        model.detail = "Check the message below and try again.";
    } else if (!status.listening) {
        model.phase = ScreenPhase::stopped;
    } else if (!status.pin.empty()) {
        model.phase = ScreenPhase::pairing;
        model.status_label = "PAIRING";
        model.headline = "Approve the connection";
        model.detail = "Enter this PIN on your Apple device.";
    } else if (model.has_video) {
        model.phase = ScreenPhase::streaming;
        model.status_label = "LIVE";
        model.headline = "Screen mirroring is live";
        model.detail = "Your Apple device is now streaming to MirrorPSX.";
    } else if (status.connected) {
        model.phase = ScreenPhase::connected;
        model.status_label = "CONNECTED";
        model.headline = "Connected. Preparing video";
        model.detail = "Waiting for the first screen-mirroring frame.";
    } else {
        model.phase = ScreenPhase::waiting;
        model.status_label = "READY";
        model.headline = "Ready to mirror";
        model.detail = "Open Screen Mirroring on your Apple device.";
    }
    return model;
}

inline void reconcile_screen_state(ScreenUiState& ui, const ScreenModel& model) {
    if (!model.allow_fullscreen) {
        ui.fullscreen = false;
        ui.focus_fullscreen = false;
    }
    if (model.can_start) ui.confirm_stop = false;
}
}
