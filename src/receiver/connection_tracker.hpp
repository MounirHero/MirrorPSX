// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/state.hpp"
#include <atomic>
#include <cstdint>

namespace mirror {
// An AirPlay session is not a single TCP connection. Discovery, pairing and
// video can open/close separate sockets; closing one must not mark the whole
// session offline while another is still active.
class ConnectionTracker {
    std::atomic<uint32_t> active_{0};
public:
    void reset(State& state) {
        active_.store(0, std::memory_order_release);
        state.update([](Snapshot& s) {
            s.connected = false;
            s.client.clear();
        });
    }
    void opened(State& state) {
        active_.fetch_add(1, std::memory_order_acq_rel);
        state.update([this](Snapshot& s) {
            s.connected = active_.load(std::memory_order_acquire) != 0;
        });
    }
    void closed(State& state) {
        uint32_t previous = active_.load(std::memory_order_acquire);
        while (previous != 0 &&
               !active_.compare_exchange_weak(previous, previous - 1,
                   std::memory_order_acq_rel, std::memory_order_acquire)) {}
        state.update([this](Snapshot& s) {
            s.connected = active_.load(std::memory_order_acquire) != 0;
            if (!s.connected) s.client.clear();
            // Never clear s.pin here: pairing can cross several TCP sockets.
        });
    }
    uint32_t active() const { return active_.load(std::memory_order_acquire); }
};
}
