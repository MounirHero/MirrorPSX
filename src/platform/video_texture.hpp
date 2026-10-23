// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef MIRROR_PS5
#include <volk.h>
#else
#include <vulkan/vulkan.h>
#endif
#include "receiver/state.hpp"
namespace mirror {
class VideoTexture {
  VkPhysicalDevice physical_;
  VkDevice device_;
  VkQueue queue_;
  uint32_t family_;
  VkImage image_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
  VkImageView view_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_ = VK_NULL_HANDLE;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkBuffer staging_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory_ = VK_NULL_HANDLE;
  void *mapped_ = nullptr;
  VkDeviceSize staging_capacity_ = 0;
  VkCommandBuffer command_ = VK_NULL_HANDLE;
  VkFence upload_fence_ = VK_NULL_HANDLE;
  int width_ = 0, height_ = 0;
  uint64_t sequence_ = 0;
  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags);
  void release();
  void staging(VkDeviceSize bytes);

public:
  VideoTexture(const VideoTexture &) = delete;
  VideoTexture &operator=(const VideoTexture &) = delete;
  VideoTexture(VkPhysicalDevice p, VkDevice d, VkQueue q, uint32_t f)
      : physical_(p), device_(d), queue_(q), family_(f) {}
  ~VideoTexture();
  void update(const State &state);
  VkDescriptorSet descriptor() const { return descriptor_; }
};
} // namespace mirror
