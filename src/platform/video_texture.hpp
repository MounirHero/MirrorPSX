// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/state.hpp"
#include <vulkan/vulkan.h>

namespace mirror {
class VideoTexture {
    VkPhysicalDevice physical_=VK_NULL_HANDLE;
    VkDevice device_=VK_NULL_HANDLE;
    VkImage image_=VK_NULL_HANDLE;
    VkDeviceMemory memory_=VK_NULL_HANDLE;
    VkImageView view_=VK_NULL_HANDLE;
    VkSampler sampler_=VK_NULL_HANDLE;
    VkDescriptorSet descriptor_=VK_NULL_HANDLE;
    int width_=0, height_=0;
    bool initialized_=false;
    void destroy_image();
public:
    VideoTexture(VkPhysicalDevice physical,VkDevice device):physical_(physical),device_(device) {}
    ~VideoTexture();
    VideoTexture(const VideoTexture&)=delete;
    VideoTexture& operator=(const VideoTexture&)=delete;
    bool upload(VkQueue queue, uint32_t family, const VideoFrame& frame);
    VkDescriptorSet descriptor() const { return descriptor_; }
    int width() const { return width_; }
    int height() const { return height_; }
};
}
