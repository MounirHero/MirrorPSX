// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/state.hpp"
#include "receiver/connection_tracker.hpp"
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

int main() {
    mirror::State state;
    mirror::VideoFrame output;
    assert(!state.frame_after(0, output));
    mirror::VideoFrame input;
    input.width = 2;
    input.height = 2;
    input.rgba.assign(16, 0xA5);
    state.publish(std::move(input));
    assert(state.snapshot().frames == 1);
    assert(state.snapshot().width == 2 && state.snapshot().height == 2);
    assert(state.frame_after(0, output));
    assert(output.sequence == 1 && output.rgba.size() == 16);
    assert(!state.frame_after(output.sequence, output));
    state.clear_frame();
    assert(state.snapshot().width == 0 && state.snapshot().height == 0);
    assert(!state.frame_after(1, output));

    std::thread writer([&] {
        for (int i = 0; i < 256; ++i) {
            mirror::VideoFrame f;
            f.width = f.height = 1;
            f.rgba.assign(4, static_cast<uint8_t>(i));
            state.publish(std::move(f));
        }
    });
    for (int i = 0; i < 256; ++i) {
        const auto status = state.snapshot();
        assert(status.frames >= 1 && status.frames <= 257);
        mirror::VideoFrame observed;
        if (state.frame_after(1, observed)) {
            assert(observed.rgba.size() == 4);
        }
    }
    writer.join();
    assert(state.snapshot().frames == 257);
    assert(state.frame_after(1, output));

    // Pairing and mirroring may use different sockets. The PIN must survive
    // closure of either connection and no premature disconnection is shown.
    mirror::ConnectionTracker connections;
    connections.opened(state);
    connections.opened(state);
    state.update([](auto& s) {
        s.pin = "0047";
        s.client = "Test client";
    });
    connections.closed(state);
    assert(connections.active() == 1);
    assert(state.snapshot().connected);
    assert(state.snapshot().pin == "0047");
    connections.closed(state);
    assert(connections.active() == 0);
    assert(!state.snapshot().connected);
    assert(state.snapshot().pin == "0047");
    assert(state.snapshot().client.empty());
    connections.closed(state);  // Defensive: spurious teardown cannot underflow.
    assert(connections.active() == 0);
    connections.reset(state);
    assert(connections.active() == 0);
    assert(output.sequence == 257);
}
