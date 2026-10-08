// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
namespace mirror {
struct VideoFrame { int width=0, height=0; uint64_t sequence=0; std::vector<uint8_t> rgba; };
struct Snapshot {
    bool listening=false, connected=false;
    std::string name="MirrorPSX", client, pin, error;
    uint16_t port=0;
    uint64_t frames=0;
    int width=0, height=0;
};
class State {
    mutable std::mutex mutex_;
    Snapshot status_;
    VideoFrame frame_;
public:
    template<class F> void update(F f) { std::lock_guard lock(mutex_); f(status_); }
    Snapshot snapshot() const { std::lock_guard lock(mutex_); return status_; }
    void publish(VideoFrame frame) {
        std::lock_guard lock(mutex_);
        frame.sequence=++status_.frames;
        status_.width=frame.width; status_.height=frame.height;
        frame_=std::move(frame);
    }
    bool frame_after(uint64_t sequence, VideoFrame& out) const {
        std::lock_guard lock(mutex_);
        if(frame_.sequence<=sequence) return false;
        out=frame_; return true;
    }
    void clear_frame() { std::lock_guard lock(mutex_); frame_={}; status_.width=status_.height=0; }
};
}
