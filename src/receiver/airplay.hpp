// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/decoder.hpp"
struct raop_s;
struct dnssd_s;
namespace mirror {
class AirPlay {
    State& state_;
    Decoder decoder_;
    raop_s* server_=nullptr;
    dnssd_s* discovery_=nullptr;
    std::mutex decode_mutex_;
public:
    explicit AirPlay(State& state):state_(state),decoder_(state) {}
    ~AirPlay();
    bool start(const std::string& name="MirrorPSX");
    void stop();
    State& state() { return state_; }
    void video(const uint8_t* data,int size);
    int codec(bool hevc);
    void flush();
};
}
