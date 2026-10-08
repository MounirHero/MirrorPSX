// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/decoder.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#include <cstring>
namespace mirror {
Decoder::~Decoder() { avcodec_free_context(&context_); av_frame_free(&frame_); sws_freeContext(scale_); }
bool Decoder::open(bool hevc) {
    avcodec_free_context(&context_); av_frame_free(&frame_);
    const AVCodec* codec=avcodec_find_decoder(hevc?AV_CODEC_ID_HEVC:AV_CODEC_ID_H264);
    if(!codec) return false;
    context_=avcodec_alloc_context3(codec); frame_=av_frame_alloc();
    if(!context_ || !frame_) return false;
    context_->thread_count=2;
    return avcodec_open2(context_,codec,nullptr)>=0;
}
void Decoder::flush() { if(context_) avcodec_flush_buffers(context_); state_.clear_frame(); }
bool Decoder::push(const uint8_t* data,int size) {
    if(!context_ || !data || size<=0 || size>16*1024*1024) return false;
    AVPacket* packet=av_packet_alloc();
    if(!packet) return false;
    if(av_new_packet(packet,size)<0) { av_packet_free(&packet); return false; }
    std::memcpy(packet->data,data,size);
    int result=avcodec_send_packet(context_,packet);
    av_packet_free(&packet);
    if(result<0) return false;
    while(avcodec_receive_frame(context_,frame_)>=0) {
        if(frame_->width<=0 || frame_->height<=0 || frame_->width>4096 || frame_->height>4096) { av_frame_unref(frame_); return false; }
        scale_=sws_getCachedContext(scale_,frame_->width,frame_->height,(AVPixelFormat)frame_->format,frame_->width,frame_->height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!scale_) { av_frame_unref(frame_); return false; }
        VideoFrame output; output.width=frame_->width; output.height=frame_->height;
        output.rgba.resize(size_t(output.width)*output.height*4);
        uint8_t* dst[]={output.rgba.data(),nullptr,nullptr,nullptr}; int stride[]={output.width*4,0,0,0};
        sws_scale(scale_,frame_->data,frame_->linesize,0,frame_->height,dst,stride);
        state_.publish(std::move(output)); av_frame_unref(frame_);
    }
    return true;
}
}
