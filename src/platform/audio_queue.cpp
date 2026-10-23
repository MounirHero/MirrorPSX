// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/audio_queue.hpp"
#ifdef MIRROR_PS5
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <ps5/audioout.h>
#include <thread>
#else
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif
namespace mirror {
struct AudioQueue::Impl {
#ifdef MIRROR_PS5
  int port = -1;
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::thread worker;
  std::array<unsigned char, 96000> ring{};
  unsigned head = 0, used = 0;
  bool paused = true, stopping = false, failed = false;
  ~Impl() {
    {
      std::lock_guard lock(mutex);
      stopping = true;
    }
    changed.notify_all();
    if (worker.joinable())
      worker.join();
    if (port >= 0) {
      sceAudioOutOutput(port, nullptr);
      sceAudioOutClose(port);
    }
  }
  void run() {
    std::array<int16_t, 2048> block{};
    for (;;) {
      {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return stopping || !paused; });
        if (stopping)
          break;
        block.fill(0);
        auto *bytes = reinterpret_cast<unsigned char *>(block.data());
        const unsigned n = used < sizeof(block) ? used : sizeof(block);
        for (unsigned i = 0; i < n; ++i)
          bytes[i] = ring[(head + i) % ring.size()];
        head = (head + n) % ring.size();
        used -= n;
      }
      // This call paces a block and can block; only this worker calls it.
      if (sceAudioOutOutput(port, block.data()) < 0) {
        std::lock_guard lock(mutex);
        failed = true;
        used = 0;
        break;
      }
    }
  }
#else
  SDL_AudioDeviceID device = 0;
  bool initialized = false;
  ~Impl() {
    if (device)
      SDL_CloseAudioDevice(device);
    if (initialized)
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
#endif
};
AudioQueue::AudioQueue() : impl_(std::make_unique<Impl>()) {}
AudioQueue::~AudioQueue() = default;
bool AudioQueue::open() {
#ifdef MIRROR_PS5
  if (impl_->port >= 0)
    return true;
  static std::once_flag initialize;
  static int initialized = -1;
  std::call_once(initialize, [] { initialized = sceAudioOutInit(); });
  if (initialized < 0)
    return false;
  impl_->port = sceAudioOutOpen(255, SCE_AUDIO_OUT_PORT_TYPE_MAIN, 0, 1024,
                                48000, SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
  if (impl_->port < 0)
    return false;
  impl_->worker = std::thread([this] { impl_->run(); });
  return true;
#else
  if (impl_->device)
    return true;
  SDL_SetMainReady();
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    return false;
  impl_->initialized = true;
  SDL_AudioSpec wanted{};
  wanted.freq = 48000;
  wanted.format = AUDIO_S16SYS;
  wanted.channels = 2;
  wanted.samples = 1024;
  impl_->device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
  return impl_->device != 0;
#endif
}
bool AudioQueue::active() const {
#ifdef MIRROR_PS5
  return impl_->port >= 0;
#else
  return impl_->device != 0;
#endif
}
void AudioQueue::pause(bool paused) {
#ifdef MIRROR_PS5
  {
    std::lock_guard lock(impl_->mutex);
    impl_->paused = paused;
  }
  impl_->changed.notify_all();
#else
  SDL_PauseAudioDevice(impl_->device, paused);
#endif
}
unsigned AudioQueue::size() const {
#ifdef MIRROR_PS5
  std::lock_guard lock(impl_->mutex);
  return impl_->used;
#else
  return SDL_GetQueuedAudioSize(impl_->device);
#endif
}
bool AudioQueue::queue(const void *data, unsigned bytes) {
#ifdef MIRROR_PS5
  std::lock_guard lock(impl_->mutex);
  if (impl_->port < 0 || impl_->failed || bytes % 4 ||
      bytes > impl_->ring.size() - impl_->used)
    return false;
  const auto *source = static_cast<const unsigned char *>(data);
  for (unsigned i = 0; i < bytes; ++i)
    impl_->ring[(impl_->head + impl_->used + i) % impl_->ring.size()] =
        source[i];
  impl_->used += bytes;
  return true;
#else
  if (bytes > 96000 || size() > 96000 - bytes)
    return false;
  return SDL_QueueAudio(impl_->device, data, bytes) == 0;
#endif
}
void AudioQueue::clear() {
#ifdef MIRROR_PS5
  std::lock_guard lock(impl_->mutex);
  impl_->head = impl_->used = 0;
#else
  SDL_ClearQueuedAudio(impl_->device);
#endif
}
} // namespace mirror
