// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/state.hpp"
struct AVCodecContext;
struct AVFrame;
struct SwsContext;
namespace mirror {
class Decoder {
    AVCodecContext* context_=nullptr;
    AVFrame* frame_=nullptr;
    SwsContext* scale_=nullptr;
    State& state_;
    bool receive_frames();
public:
    explicit Decoder(State& state):state_(state) {}
    ~Decoder();
    bool open(bool hevc=false);
    bool push(const uint8_t* data, int size);
    void flush();
};
}
