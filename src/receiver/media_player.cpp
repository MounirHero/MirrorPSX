// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/media_player.hpp"
#include "receiver/frame_converter.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include "platform/audio_queue.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>

namespace mirror {
using Clock = std::chrono::steady_clock;
struct MediaPlayer::Impl {
  State &state;
  mutable std::mutex mutex;
  std::condition_variable changed;
  MediaStatus status;
  std::atomic<bool> shutdown{false}, cancel{false};
  bool playing = false;
  uint64_t generation = 0;
  double seek_target = -1;
  std::atomic<int64_t> deadline{0};
  std::thread worker;
  static int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now().time_since_epoch())
        .count();
  }
  explicit Impl(State &s) : state(s), worker([this] { run(); }) {}
  ~Impl() {
    shutdown = true;
    cancel = true;
    changed.notify_all();
    worker.join();
  }
  static int interrupt(void *ptr) {
    auto &self = *static_cast<Impl *>(ptr);
    return self.shutdown || self.cancel || now() > self.deadline;
  }
  void error(const char *message, uint64_t revision) {
    std::lock_guard lock(mutex);
    if (revision != generation)
      return;
    status.error = message;
    status.transport = "STOPPED";
    playing = false;
    state.update([&](auto &s) {
      s.error = message;
      s.media_paused = false;
      s.media_loading = false;
      s.connected = false;
    });
    state.clear_frame();
  }
  void session(const std::string &uri, uint64_t revision) {
    struct Resources {
      AVFormatContext *format = nullptr;
      AVCodecContext *video = nullptr, *audio = nullptr;
      AVPacket *packet = av_packet_alloc();
      AVFrame *frame = av_frame_alloc();
      FrameConverter converter;
      SwrContext *resample = nullptr;
      AudioQueue output;
      ~Resources() {
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        avformat_close_input(&format);
        av_packet_free(&packet);
        av_frame_free(&frame);
        swr_free(&resample);
      }
    } r;
    if (!r.packet || !r.frame) {
      error("Memoria insufficiente per il video.", revision);
      return;
    }
    r.format = avformat_alloc_context();
    if (!r.format) {
      error("Memoria insufficiente per il video.", revision);
      return;
    }
    r.format->interrupt_callback = {interrupt, this};
    AVDictionary *options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "http,https,tcp,tls,crypto", 0);
    av_dict_set(&options, "rw_timeout", "5000000", 0);
    deadline = now() + 8000;
    int result = avformat_open_input(&r.format, uri.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (result < 0 || avformat_find_stream_info(r.format, nullptr) < 0) {
      error("Impossibile aprire il video. Usa un URL diretto non protetto.",
            revision);
      return;
    }
    auto open = [&](AVMediaType type, AVCodecContext *&codec) {
      int index = av_find_best_stream(r.format, type, -1, -1, nullptr, 0);
      if (index < 0)
        return -1;
      auto *decoder =
          avcodec_find_decoder(r.format->streams[index]->codecpar->codec_id);
      if (!decoder)
        return -1;
      codec = avcodec_alloc_context3(decoder);
      if (!codec)
        return -1;
      if (avcodec_parameters_to_context(codec,
                                        r.format->streams[index]->codecpar) < 0)
        return -1;
      codec->thread_count = 2;
      if (avcodec_open2(codec, decoder, nullptr) < 0)
        return -1;
      return index;
    };
    int vi = open(AVMEDIA_TYPE_VIDEO, r.video),
        ai = open(AVMEDIA_TYPE_AUDIO, r.audio);
    if (vi < 0) {
      error("Questo contenuto non contiene un video supportato.", revision);
      return;
    }
    if (ai >= 0) {
      if (!r.output.open()) {
        error("Uscita audio non disponibile.", revision);
        return;
      }
      AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
      if (swr_alloc_set_opts2(&r.resample, &stereo, AV_SAMPLE_FMT_S16, 48000,
                              &r.audio->ch_layout, r.audio->sample_fmt,
                              r.audio->sample_rate, 0, nullptr) < 0 ||
          swr_init(r.resample) < 0) {
        error("Formato audio non supportato.", revision);
        return;
      }
      r.output.pause(false);
    }
    {
      std::lock_guard lock(mutex);
      if (revision != generation)
        return;
      status.duration = r.format->duration > 0
                            ? double(r.format->duration) / AV_TIME_BASE
                            : 0;
    }
    auto origin = Clock::now();
    double first_pts = -1;
    std::vector<uint8_t> audio;
    auto drain = [&](AVCodecContext *codec, int index) {
      while (avcodec_receive_frame(codec, r.frame) >= 0 && !cancel &&
             !shutdown) {
        if (index == vi) {
          double pts = r.frame->best_effort_timestamp == AV_NOPTS_VALUE
                           ? 0
                           : r.frame->best_effort_timestamp *
                                 av_q2d(r.format->streams[vi]->time_base);
          if (first_pts < 0) {
            first_pts = pts;
            origin = Clock::now();
          }
          auto due =
              origin +
              std::chrono::duration_cast<Clock::duration>(
                  std::chrono::duration<double>(std::max(0., pts - first_pts)));
          {
            std::unique_lock lock(mutex);
            changed.wait_until(lock, due, [&] {
              return shutdown || cancel || !playing || seek_target >= 0;
            });
            if (shutdown || cancel)
              return false;
            if (!playing || seek_target >= 0) {
              av_frame_unref(r.frame);
              continue;
            }
            status.position = pts;
          }
          // Skip late video while preserving packet decoding/reference
          // frames.
          if (Clock::now() - due > std::chrono::milliseconds(150)) {
            av_frame_unref(r.frame);
            continue;
          }
          if (r.frame->width <= 0 || r.frame->height <= 0 ||
              r.frame->width > 4096 || r.frame->height > 4096) {
            error("Risoluzione video non supportata (massimo 4096×4096).",
                  revision);
            return false;
          }
          VideoFrame frame;
          if (!r.converter.convert(*r.frame, frame)) {
            error("Conversione video non disponibile.", revision);
            return false;
          }
          std::lock_guard lock(mutex);
          if (revision != generation || cancel || shutdown)
            return false;
          state.publish(std::move(frame));
        } else if (r.output.active()) {
          int count = swr_get_out_samples(r.resample, r.frame->nb_samples);
          if (count <= 0 || count > 24000) {
            error("Blocco audio non valido.", revision);
            return false;
          }
          audio.resize(size_t(count) * 4);
          uint8_t *dst[] = {audio.data()};
          count =
              swr_convert(r.resample, dst, count,
                          const_cast<const uint8_t **>(r.frame->extended_data),
                          r.frame->nb_samples);
          if (count < 0) {
            error("Decodifica audio fallita.", revision);
            return false;
          }
          int volume;
          {
            std::lock_guard lock(mutex);
            volume = status.muted ? 0 : status.volume;
          }
          auto *samples = reinterpret_cast<int16_t *>(audio.data());
          for (int n = 0; n < count * 2; ++n)
            samples[n] = int16_t(int(samples[n]) * volume / 100);
          // Queue at most half a second, including the next decoded block.
          while (r.output.size() + unsigned(count * 4) > 96000 && !cancel &&
                 !shutdown) {
            std::unique_lock lock(mutex);
            changed.wait_for(lock, std::chrono::milliseconds(5),
                             [&] { return cancel || shutdown || !playing; });
            if (!playing) {
              if (r.output.active())
                r.output.pause(true);
              changed.wait(lock, [&] { return playing || cancel || shutdown; });
              if (r.output.active())
                r.output.pause(false);
            }
          }
          if (!cancel && !shutdown &&
              !r.output.queue(audio.data(), unsigned(count * 4))) {
            error("Riproduzione audio fallita.", revision);
            return false;
          }
        }
        av_frame_unref(r.frame);
      }
      return true;
    };
    while (!shutdown && !cancel) {
      {
        std::unique_lock lock(mutex);
        if (revision != generation)
          return;
        if (!playing) {
          if (r.output.active())
            r.output.pause(true);
          auto before = Clock::now();
          changed.wait(lock, [&] {
            return playing || shutdown || cancel || revision != generation;
          });
          origin += Clock::now() - before;
          if (r.output.active())
            r.output.pause(false);
        }
        if (shutdown || cancel || revision != generation)
          return;
        if (seek_target >= 0) {
          auto target = seek_target;
          seek_target = -1;
          lock.unlock();
          deadline = now() + 5000;
          if (av_seek_frame(r.format, -1, int64_t(target * AV_TIME_BASE),
                            AVSEEK_FLAG_BACKWARD) >= 0) {
            avcodec_flush_buffers(r.video);
            if (r.audio)
              avcodec_flush_buffers(r.audio);
            if (r.output.active())
              r.output.clear();
            first_pts = -1;
            origin = Clock::now();
          }
        }
      }
      deadline = now() + 5000;
      result = av_read_frame(r.format, r.packet);
      if (result < 0) {
        if (result != AVERROR_EOF && !cancel && !shutdown)
          error("La connessione al video è stata interrotta.", revision);
        break;
      }
      auto *codec = r.packet->stream_index == vi   ? r.video
                    : r.packet->stream_index == ai ? r.audio
                                                   : nullptr;
      int index = r.packet->stream_index;
      if (codec && avcodec_send_packet(codec, r.packet) >= 0) {
        if (!drain(codec, index))
          return;
      }
      av_packet_unref(r.packet);
    }
    // Let the bounded audio tail finish before marking the media stopped.
    while (r.output.active() && r.output.size() > 0 && !cancel && !shutdown) {
      std::unique_lock lock(mutex);
      changed.wait_for(lock, std::chrono::milliseconds(10));
    }
    std::lock_guard lock(mutex);
    if (revision == generation) {
      playing = false;
      status.transport = "STOPPED";
      state.update([](auto &s) { s.media_paused = s.connected; });
    }
  }
  void run() {
    uint64_t processed = 0;
    while (!shutdown) {
      std::unique_lock lock(mutex);
      changed.wait(lock, [&] {
        return shutdown || (playing && generation != processed);
      });
      if (shutdown)
        return;
      auto uri = status.uri;
      processed = generation;
      cancel = false;
      lock.unlock();
      session(uri, processed);
    }
  }
};
MediaPlayer::MediaPlayer(State &s) : impl_(std::make_unique<Impl>(s)) {}
MediaPlayer::~MediaPlayer() = default;
bool MediaPlayer::load(const std::string &uri, const std::string &title) {
  if (uri.size() > 8192 ||
      (uri.rfind("http://", 0) != 0 && uri.rfind("https://", 0) != 0))
    return false;
  auto &p = *impl_;
  std::lock_guard lock(p.mutex);
  p.cancel = true;
  ++p.generation;
  p.playing = false;
  p.seek_target = -1;
  p.status.uri = uri;
  p.status.title = title.substr(0, 256);
  p.status.position = p.status.duration = 0;
  p.status.error.clear();
  p.status.transport = "STOPPED";
  p.state.clear_frame();
  p.state.update([&](auto &s) {
    s.error.clear();
    s.connected = false;
    s.client = p.status.title.empty() ? "Video DLNA" : p.status.title;
    s.pin.clear();
    s.media_paused = false;
  });
  p.changed.notify_all();
  return true;
}
void MediaPlayer::play() {
  auto &p = *impl_;
  std::lock_guard lock(p.mutex);
  if (p.status.uri.empty())
    return;
  if (!p.playing && p.status.transport == "STOPPED") {
    p.cancel = true;
    ++p.generation;
  }
  p.playing = true;
  bool loading = p.status.transport == "STOPPED";
  p.status.error.clear();
  p.status.transport = "PLAYING";
  p.state.update([&](auto &s) {
    s.media_paused = false;
    s.media_loading = loading;
    s.error.clear();
  });
  p.changed.notify_all();
}
void MediaPlayer::pause() {
  auto &p = *impl_;
  std::lock_guard lock(p.mutex);
  if (p.status.transport != "PLAYING")
    return;
  p.playing = false;
  p.status.transport = "PAUSED_PLAYBACK";
  p.state.update([](auto &s) {
    s.media_paused = true;
    s.media_loading = false;
  });
  p.changed.notify_all();
}
void MediaPlayer::stop() {
  auto &p = *impl_;
  std::lock_guard lock(p.mutex);
  p.cancel = true;
  p.playing = false;
  ++p.generation;
  p.seek_target = -1;
  p.status.transport = p.status.uri.empty() ? "NO_MEDIA_PRESENT" : "STOPPED";
  p.status.position = 0;
  p.changed.notify_all();
  p.state.clear_frame();
  p.state.update([](auto &s) {
    s.connected = false;
    s.media_paused = false;
  });
}
bool MediaPlayer::seek(double seconds) {
  auto &p = *impl_;
  std::lock_guard lock(p.mutex);
  if (seconds < 0 || p.status.duration <= 0 || seconds > p.status.duration)
    return false;
  p.seek_target = seconds;
  p.changed.notify_all();
  return true;
}
void MediaPlayer::volume(int value) {
  auto &p = *impl_;
  std::lock_guard lock(p.mutex);
  p.status.volume = std::clamp(value, 0, 100);
}
void MediaPlayer::mute(bool value) {
  auto &p = *impl_;
  std::lock_guard lock(p.mutex);
  p.status.muted = value;
}
MediaStatus MediaPlayer::status() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->status;
}
} // namespace mirror
