// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/airplay.hpp"
#include "ui/screen_model.hpp"
#include <cassert>

int main() {
    mirror::State state;
    mirror::AirPlay receiver(state);

    state.update([](auto& s) {
        s.listening = true;
        s.connected = true;
        s.pin = "0047";
    });
    receiver.stream_running(true);
    assert(state.snapshot().pin.empty());

    mirror::VideoFrame video;
    video.width = 640;
    video.height = 360;
    video.rgba.resize(640 * 360 * 4);
    state.publish(std::move(video));
    assert(mirror::make_screen_model(state.snapshot(), true).phase ==
           mirror::ScreenPhase::streaming);

    // RTP may terminate while a control socket remains connected. The UI
    // must stop advertising stale video instead of becoming stuck in LIVE.
    receiver.stream_running(false);
    const auto snapshot = state.snapshot();
    assert(snapshot.width == 0 && snapshot.height == 0);
    assert(mirror::make_screen_model(snapshot, false).phase ==
           mirror::ScreenPhase::connected);
    mirror::VideoFrame dropped;
    assert(!state.frame_after(0, dropped));
    receiver.stream_running(false); // Repeated transport teardown is safe.
    receiver.stop();                // No active UxPlay host in this test.
    assert(!state.snapshot().listening);
    assert(mirror::make_screen_model(state.snapshot(), false).phase ==
           mirror::ScreenPhase::stopped);
}
