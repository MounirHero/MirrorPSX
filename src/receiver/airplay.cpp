// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/airplay.hpp"
extern "C" {
#include "logger.h"
#include "raop.h"
}
#include <cstdio>
#include <cstring>
#ifdef MIRROR_PS5
#include "platform/ps5_broker.hpp"
#include "platform/ps5_random.hpp"
#include <sys/stat.h>
#endif
namespace mirror {
AirPlay::~AirPlay() { stop(); }
void AirPlay::video(const uint8_t *data, int size) {
  std::lock_guard lock(decode_mutex_);
  decoder_.push(data, size);
}
int AirPlay::codec(bool hevc) {
  std::lock_guard lock(decode_mutex_);
  return decoder_.open(hevc) ? 0 : -1;
}
void AirPlay::flush() {
  std::lock_guard lock(decode_mutex_);
  decoder_.flush();
  state_.update([](auto &s) { s.connected = false; });
}
void AirPlay::stream_running(bool running) {
  if (running) {
    state_.update([](auto &s) { s.pin.clear(); });
  } else {
    // Never leave a frozen last frame marked as LIVE after a stream ends.
    flush();
  }
}
bool AirPlay::start(const std::string &name) {
  stop();
  state_.update([&](auto &s) {
    s.name = name;
    s.error.clear();
    s.port = 0;
  });
#ifdef MIRROR_PS5
  std::string broker_error;
  if (!prepare_ps5_network(broker_error)) {
    state_.update([&](auto &s) { s.error = broker_error; });
    return false;
  }
  if (!initialize_ps5_random()) {
    state_.update([](auto &s) {
      s.error = "Abbinamento non disponibile: accesso al generatore sicuro PS5 "
                "fallito.";
    });
    return false;
  }
#endif
  if (!decoder_.open()) {
    state_.update([](auto &s) { s.error = "Decoder H.264 non disponibile"; });
    return false;
  }
  raop_callbacks_t cb{};
  cb.cls = this;
  cb.video_process = [](void *p, raop_ntp_t *, video_decode_struct *v) {
    static_cast<AirPlay *>(p)->video(v->data, v->data_len);
  };
  cb.audio_process = [](void *, raop_ntp_t *, audio_decode_struct *) {};
  cb.conn_init = [](void *p) {
    auto *a = static_cast<AirPlay *>(p);
    a->connections_.opened(a->state());
  };
  // Pairing closes its first control socket before the user enters the PIN.
  // Keep that PIN visible until video arrives, a new PIN replaces it, or
  // stop().
  cb.conn_destroy = [](void *p) {
    auto *a = static_cast<AirPlay *>(p);
    a->connections_.closed(a->state());
    if (a->connections_.active() == 0)
      a->flush();
  };
  cb.report_client_request = [](void *p, char *, char *, char *name,
                                bool *admit) {
    *admit = true;
    static_cast<AirPlay *>(p)->state().update(
        [&](auto &s) { s.client = name ? name : "Dispositivo Apple"; });
  };
  cb.display_pin = [](void *p, char *pin) {
    static_cast<AirPlay *>(p)->state().update(
        [&](auto &s) { s.pin = pin ? pin : ""; });
  };
  cb.mirror_video_running = [](void *p, bool running) {
    static_cast<AirPlay *>(p)->stream_running(running);
  };
  cb.video_set_codec = [](void *p, video_codec_t c) {
    return static_cast<AirPlay *>(p)->codec(c == VIDEO_CODEC_H265);
  };
  cb.video_flush = [](void *p) { static_cast<AirPlay *>(p)->flush(); };
  cb.video_reset = [](void *p, reset_type_t) {
    static_cast<AirPlay *>(p)->flush();
  };
  cb.conn_reset = [](void *p, int) { static_cast<AirPlay *>(p)->flush(); };
  cb.conn_feedback = [](void *) {};
  cb.video_pause = [](void *) {};
  cb.video_resume = [](void *) {};
  cb.audio_flush = [](void *) {};
  cb.audio_get_format = [](void *, unsigned char *, unsigned short *, bool *,
                           bool *, uint64_t *) {};
  cb.audio_set_volume = [](void *, float) {};
  cb.audio_set_client_volume = [](void *) { return 0.0; };
  cb.video_report_size = [](void *, float *, float *, float *, float *) {};
  cb.check_register = [](void *, const char *) { return false; };
  cb.register_client = [](void *p, const char *, const char *, const char *) {
    static_cast<AirPlay *>(p)->state().update([](auto &s) { s.pin.clear(); });
  };
  cb.passwd = [](void *, int *len) -> const char * {
    *len = 0;
    return nullptr;
  };
  server_ = raop_init(&cb);
  if (!server_) {
    state_.update([](auto &s) { s.error = "Avvio AirPlay fallito"; });
    return false;
  }
  raop_set_log_level(server_, LOGGER_INFO);
  raop_set_log_callback(
      server_,
      [](void *, int, const char *msg) {
        if (!msg)
          return;
        if (std::strstr(msg, "CLIENT MUST NOW ENTER PIN")) {
          std::fprintf(stderr,
                       "[AirPlay] Pairing PIN requested (shown in UI)\n");
          return;
        }
        std::fprintf(stderr, "[AirPlay] %s\n", msg);
      },
      nullptr);
  // Stable locally administered identity; persistent pairing key stored by
  // UxPlay.
#ifdef MIRROR_PS5
  constexpr const char *identity = "02:4D:50:53:58:02";
  constexpr const char *key_path = "/data/mirrorpsx/pairing.key";
  mkdir("/data/mirrorpsx", 0700);
  const char mac[] = {0x02, 0x4d, 0x50, 0x53, 0x58, 0x02};
#else
  constexpr const char *identity = "02:4D:50:53:58:01";
  constexpr const char *key_path = "mirrorpsx.key";
  const char mac[] = {0x02, 0x4d, 0x50, 0x53, 0x58, 0x01};
#endif
  if (raop_init2(server_, 0, identity, key_path) != 0) {
    stop();
    state_.update([](auto &s) {
      s.error = "Impossibile inizializzare le chiavi di collegamento.";
    });
    return false;
  }
  int error = 0;
  discovery_ = dnssd_init(name.c_str(), int(name.size()), mac, 6, 1, &error);
  if (!discovery_ || error) {
    stop();
    state_.update([&](auto &s) {
      s.error = "Scoperta mDNS non disponibile (" + std::to_string(error) + ")";
    });
    return false;
  }
  dnssd_set_airplay_features(discovery_, 0, 0);
  dnssd_set_airplay_features(discovery_, 4, 0);
  dnssd_set_airplay_features(discovery_, 42, 0);
  raop_set_dnssd(server_, discovery_);
  raop_set_plist(server_, "width", 1920);
  raop_set_plist(server_, "height", 1080);
  raop_set_plist(server_, "maxFPS", 30);
  raop_set_plist(server_, "pin", 1);
  // The OS selects available ports; fixed 7100-7104 can be reserved by Windows.
  // UxPlay reports the allocated media ports during setup and DNS-SD advertises
  // HTTP.
  unsigned short tcp[2] = {0, 0}, udp[3] = {0, 0, 0};
  raop_set_tcp_ports(server_, tcp);
  raop_set_udp_ports(server_, udp);
  unsigned short port = 0;
  if (raop_start_httpd(server_, &port) < 0) {
    stop();
    state_.update([](auto &s) {
      s.error = "Impossibile aprire il servizio di rete. Controlla il log del "
                "ricevitore.";
    });
    return false;
  }
  raop_set_port(server_, port);
  const int raop_announcement = dnssd_register_raop(discovery_, port);
  const int airplay_announcement =
      raop_announcement == 0 ? dnssd_register_airplay(discovery_, port) : 0;
  if (raop_announcement != 0 || airplay_announcement != 0) {
    const int announcement_error =
        raop_announcement != 0 ? raop_announcement : airplay_announcement;
    std::fprintf(stderr, "[MirrorPSX] mDNS registration: RAOP=%d AirPlay=%d\n",
                 raop_announcement, airplay_announcement);
    stop();
    state_.update([&](auto &s) {
      s.error = "Annuncio in rete fallito (" +
                std::to_string(announcement_error) + ")";
    });
    return false;
  }
  state_.update([&](auto &s) {
    s.listening = true;
    s.name = name;
    s.port = port;
    s.error.clear();
  });
  std::fprintf(stderr,
               "[MirrorPSX] Receiver ready: %s, TCP %u, DNS-SD registered\n",
               name.c_str(), port);
  return true;
}
void AirPlay::stop() {
  if (discovery_) {
    dnssd_unregister_airplay(discovery_);
    dnssd_unregister_raop(discovery_);
  }
  if (server_) {
    raop_destroy(server_);
    server_ = nullptr;
  }
  if (discovery_) {
    dnssd_destroy(discovery_);
    discovery_ = nullptr;
  }
  flush();
  connections_.reset(state_);
  state_.update([](auto &s) {
    s.listening = false;
    s.connected = false;
    s.port = 0;
    s.client.clear();
    s.pin.clear();
  });
}
} // namespace mirror
