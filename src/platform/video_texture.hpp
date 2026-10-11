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
    VkBuffer upload_buffer_=VK_NULL_HANDLE;
    VkDeviceMemory upload_memory_=VK_NULL_HANDLE;
    VkDeviceSize upload_capacity_=0;
    VkCommandPool command_pool_=VK_NULL_HANDLE;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    int width_=0, height_=0;
    bool initialized_=false;
    void destroy_image();
    void destroy_upload();
    bool prepare_upload(VkDeviceSize size, uint32_t family);
public:
    VideoTexture(VkPhysicalDevice physical,VkDevice device):physical_(physical),device_(device) {}
    ~VideoTexture();
    VideoTexture(const VideoTexture&)=delete;
    VideoTexture& operator=(const VideoTexture&)=delete;
    bool upload(VkQueue queue, uint32_t family, const VideoFrame& frame);
    // Discard sampled video and CPU staging resources on stream teardown.
    // Safe to call repeatedly; device/descriptor ownership stays with the host.
    void clear();
    VkDescriptorSet descriptor() const { return descriptor_; }
    int width() const { return width_; }
    int height() const { return height_; }
};
}
