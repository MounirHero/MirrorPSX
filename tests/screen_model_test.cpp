// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screen_model.hpp"
#include <cassert>

int main() {
    mirror::Snapshot s;
    assert(mirror::make_screen_model(s, false).phase == mirror::ScreenPhase::stopped);
    s.error="Port unavailable";
    assert(mirror::make_screen_model(s, false).phase == mirror::ScreenPhase::error);
    s.error.clear();
    s.listening=true;
    assert(mirror::make_screen_model(s, false).phase == mirror::ScreenPhase::waiting);
    s.connected=true;
    assert(mirror::make_screen_model(s, false).phase == mirror::ScreenPhase::connected);
    s.pin="2468";
    assert(mirror::make_screen_model(s, false).phase == mirror::ScreenPhase::pairing);
    s.pin.clear(); s.width=1920; s.height=1080; s.frames=42;
    assert(mirror::make_screen_model(s, false).phase == mirror::ScreenPhase::connected);
    const auto live=mirror::make_screen_model(s, true);
    assert(live.phase == mirror::ScreenPhase::streaming);
    assert(live.allow_fullscreen && live.has_video);
    mirror::ScreenUiState ui;
    ui.fullscreen=true;
    mirror::reconcile_screen_state(ui, live);
    assert(ui.fullscreen);
    s.listening=false;
    mirror::reconcile_screen_state(ui, mirror::make_screen_model(s, false));
    assert(!ui.fullscreen);
}
