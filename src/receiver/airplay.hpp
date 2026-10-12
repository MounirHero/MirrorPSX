// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/decoder.hpp"
#include "receiver/connection_tracker.hpp"
struct raop_s;
struct dnssd_s;
namespace mirror {
class AirPlay {
    State& state_;
    Decoder decoder_;
    ConnectionTracker connections_;
    raop_s* server_=nullptr;
    dnssd_s* discovery_=nullptr;
    std::mutex decode_mutex_;
public:
    explicit AirPlay(State& state):state_(state),decoder_(state) {}
    ~AirPlay();
    bool start(const std::string& name="MirrorPSX");
    void stop();
    State& state() { return state_; }
    ConnectionTracker& connections() { return connections_; }
    void video(const uint8_t* data,int size);
    int codec(bool hevc);
    void flush();
    // Stream transport events, distinct from transient RTSP socket lifetime.
    void stream_running(bool running);
};
}
