// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/airplay.hpp"
extern "C" {
#include "raop.h"
#include "logger.h"
}
#include <cstdio>
#include <cstring>
namespace mirror {
AirPlay::~AirPlay() { stop(); }
void AirPlay::video(const uint8_t* data,int size) { std::lock_guard lock(decode_mutex_); decoder_.push(data,size); }
int AirPlay::codec(bool hevc) { std::lock_guard lock(decode_mutex_); return decoder_.open(hevc)?0:-1; }
void AirPlay::flush() { std::lock_guard lock(decode_mutex_); decoder_.flush(); }
bool AirPlay::start(const std::string& name) {
    stop();
    { std::lock_guard lock(decode_mutex_); if(!decoder_.open()) { state_.update([](auto& s){s.error="H.264 decoder unavailable";}); return false; } }
    raop_callbacks_t cb{}; cb.cls=this;
    cb.video_process=[](void* p,raop_ntp_t*,video_decode_struct* v){static_cast<AirPlay*>(p)->video(v->data,v->data_len);};
    cb.audio_process=[](void*,raop_ntp_t*,audio_decode_struct*){};
    cb.conn_init=[](void* p){static_cast<AirPlay*>(p)->state().update([](auto& s){s.connected=true;});};
    // Pairing can span more than one RTSP/TCP connection: clearing the PIN
    // whenever a socket closes makes it disappear before the user can type it.
    cb.conn_destroy=[](void* p){auto* a=static_cast<AirPlay*>(p); a->flush(); a->state().update([](auto& s){s.connected=false;s.client.clear();});};
    cb.report_client_request=[](void* p,char*,char*,char* name,bool* admit){ *admit=true; static_cast<AirPlay*>(p)->state().update([&](auto& s){s.client=name?name:"Dispositivo Apple";}); };
    cb.display_pin=[](void* p,char* pin){static_cast<AirPlay*>(p)->state().update([&](auto& s){s.pin=pin?pin:"";});};
    cb.mirror_video_running=[](void* p,bool running){
        if(running) static_cast<AirPlay*>(p)->state().update([](auto& s){s.pin.clear();});
    };
    cb.video_set_codec=[](void* p,video_codec_t c){return static_cast<AirPlay*>(p)->codec(c==VIDEO_CODEC_H265);};
    cb.video_flush=[](void* p){static_cast<AirPlay*>(p)->flush();};
    cb.video_reset=[](void* p,reset_type_t){static_cast<AirPlay*>(p)->flush();};
    cb.conn_reset=[](void* p,int){static_cast<AirPlay*>(p)->flush();};
    cb.conn_feedback=[](void*){};
    cb.video_pause=[](void*){}; cb.video_resume=[](void*){};
    cb.audio_flush=[](void*){};
    cb.audio_get_format=[](void*,unsigned char* ct,unsigned short* spf,bool* screen,bool* media,uint64_t* fmt){if(ct)*ct=0;if(spf)*spf=0;if(screen)*screen=true;if(media)*media=false;if(fmt)*fmt=0;};
    cb.audio_set_volume=[](void*,float){};
    cb.audio_set_client_volume=[](void*){return 0.0;};
    cb.video_report_size=[](void*,float* sw,float* sh,float* w,float* h){if(sw)*sw=1920;if(sh)*sh=1080;if(w)*w=1920;if(h)*h=1080;};
    cb.check_register=[](void*,const char*){return false;};
    cb.register_client=[](void* p,const char*,const char*,const char*){
        static_cast<AirPlay*>(p)->state().update([](auto& s){s.pin.clear();});
    };
    cb.passwd=[](void*,int* len)->const char*{*len=0;return nullptr;};
    server_=raop_init(&cb);
    if(!server_) { state_.update([](auto& s){s.error="AirPlay initialization failed";}); return false; }
    raop_set_log_level(server_,LOGGER_INFO);
    // UxPlay's info logger otherwise writes the *actual* pairing secret to
    // logs. Keep diagnostics useful without disclosing the displayed PIN.
    raop_set_log_callback(server_,[](void*,int,const char* msg){
        if (!msg) return;
        if (std::strstr(msg, "CLIENT MUST NOW ENTER PIN")) {
            std::fprintf(stderr, "[AirPlay] Pairing PIN requested (shown in UI)\n");
        } else {
            std::fprintf(stderr, "[AirPlay] %s\n", msg);
        }
    },nullptr);
    // Stable locally administered identity; persistent pairing key stored by UxPlay.
    if(raop_init2(server_,0,"02:4D:50:53:58:01","mirrorpsx.key")!=0) { stop(); state_.update([](auto& s){s.error="AirPlay pairing initialization failed";}); return false; }
    int error=0; const char mac[]={0x02,0x4d,0x50,0x53,0x58,0x01};
    discovery_=dnssd_init(name.c_str(),int(name.size()),mac,6,1,&error);
    if(!discovery_ || error) { stop(); state_.update([&](auto& s){s.error="Scoperta mDNS non disponibile ("+std::to_string(error)+")";}); return false; }
    dnssd_set_airplay_features(discovery_,0,0); dnssd_set_airplay_features(discovery_,4,0);
    dnssd_set_airplay_features(discovery_,42,0);
    raop_set_dnssd(server_,discovery_);
    raop_set_plist(server_,"width",1920); raop_set_plist(server_,"height",1080); raop_set_plist(server_,"maxFPS",30);
    raop_set_plist(server_,"pin",1);
    // Let the OS assign unused ports. Fixed 7100-7104 falls in common Windows
    // excluded-port ranges (10013 / WSAEACCES). UxPlay advertises the chosen
    // HTTP port through mDNS, while RTP/mirroring callbacks negotiate their
    // separately allocated sockets with the client.
    unsigned short tcp[2]={0,0},udp[3]={0,0,0};
    raop_set_tcp_ports(server_,tcp); raop_set_udp_ports(server_,udp);
    unsigned short port=0;
    if(raop_start_httpd(server_,&port)<0) { stop(); state_.update([](auto& s){s.error="AirPlay TCP port unavailable";}); return false; }
    raop_set_port(server_,port);
    if(dnssd_register_raop(discovery_,port)!=0 || dnssd_register_airplay(discovery_,port)!=0) { stop(); state_.update([](auto& s){s.error="AirPlay network advertisement failed";}); return false; }
    state_.update([&](auto& s){s.listening=true;s.name=name;s.port=port;s.error.clear();});
    return true;
}
void AirPlay::stop() {
    if(discovery_) { dnssd_unregister_airplay(discovery_); dnssd_unregister_raop(discovery_); }
    if(server_) { raop_destroy(server_);server_=nullptr; }
    if(discovery_) { dnssd_destroy(discovery_);discovery_=nullptr; }
    flush();
    state_.update([](auto& s){s.listening=false;s.connected=false;s.client.clear();s.pin.clear();});
}
}
