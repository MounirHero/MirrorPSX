// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/decoder.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int argc,char** argv){
    mirror::State state;mirror::Decoder decoder(state);
    if(!decoder.open() || decoder.push(nullptr,0))return 1;
    if(argc==2){std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),{});if(data.empty()||!decoder.push(data.data(),int(data.size())))return 2;mirror::VideoFrame frame;if(!state.frame_after(0,frame)||frame.width!=320||frame.height!=180||frame.rgba.size()!=320*180*4)return 3;}
    decoder.flush();mirror::VideoFrame frame;if(state.frame_after(state.snapshot().frames,frame))return 4;
    std::puts("decoder: passed");
}
