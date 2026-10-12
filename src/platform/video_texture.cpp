// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/video_texture.hpp"
#include "imgui_impl_vulkan.h"
#include <cstring>
#include <cstdio>

namespace mirror {
namespace {
uint32_t find_memory(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    return UINT32_MAX;
}
}

void VideoTexture::destroy_image() {
    if (descriptor_) ImGui_ImplVulkan_RemoveTexture(descriptor_);
    if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
    if (view_) vkDestroyImageView(device_, view_, nullptr);
    if (image_) vkDestroyImage(device_, image_, nullptr);
    if (memory_) vkFreeMemory(device_, memory_, nullptr);
    descriptor_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    width_ = height_ = 0;
    initialized_ = false;
}

void VideoTexture::destroy_upload() {
    if (upload_fence_) vkDestroyFence(device_, upload_fence_, nullptr);
    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
    if (upload_buffer_) vkDestroyBuffer(device_, upload_buffer_, nullptr);
    if (upload_memory_) vkFreeMemory(device_, upload_memory_, nullptr);
    command_pool_ = VK_NULL_HANDLE;
    command_ = VK_NULL_HANDLE;
    upload_fence_ = VK_NULL_HANDLE;
    upload_in_flight_ = false;
    upload_queue_ = VK_NULL_HANDLE;
    upload_family_ = UINT32_MAX;
    upload_buffer_ = VK_NULL_HANDLE;
    upload_memory_ = VK_NULL_HANDLE;
    upload_capacity_ = 0;
}

VideoTexture::~VideoTexture() {
    clear();
}

void VideoTexture::clear() {
    if (!image_ && !memory_ && !view_ && !sampler_ && !descriptor_ &&
        !upload_buffer_ && !upload_memory_ && !command_pool_ && !upload_fence_) {
        upload_queue_ = VK_NULL_HANDLE;
        upload_family_ = UINT32_MAX;
        return;
    }
    if (device_) vkDeviceWaitIdle(device_);
    destroy_image();
    destroy_upload();
}

bool VideoTexture::prepare_upload(VkDeviceSize bytes, uint32_t family) {
    if (!command_pool_) {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = family;
        if (vkCreateCommandPool(device_, &pi, nullptr, &command_pool_) != VK_SUCCESS) return false;
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = command_pool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device_, &ai, &command_) != VK_SUCCESS) return false;
    }
    if (!upload_fence_) {
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (vkCreateFence(device_, &fi, nullptr, &upload_fence_) != VK_SUCCESS) return false;
    }
    if (upload_buffer_ && upload_capacity_ >= bytes) return true;
    if (upload_buffer_) vkDestroyBuffer(device_, upload_buffer_, nullptr);
    if (upload_memory_) vkFreeMemory(device_, upload_memory_, nullptr);
    upload_buffer_ = VK_NULL_HANDLE;
    upload_memory_ = VK_NULL_HANDLE;
    upload_capacity_ = 0;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, nullptr, &upload_buffer_) != VK_SUCCESS) return false;
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, upload_buffer_, &req);
    const auto type = find_memory(physical_, req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == UINT32_MAX) return false;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &ai, nullptr, &upload_memory_) != VK_SUCCESS ||
        vkBindBufferMemory(device_, upload_buffer_, upload_memory_, 0) != VK_SUCCESS) return false;
    upload_capacity_ = bytes;
    return true;
}

bool VideoTexture::upload(VkQueue queue, uint32_t family, const VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.width > 4096 || frame.height > 4096 ||
        frame.rgba.size() != size_t(frame.width) * size_t(frame.height) * 4u) return false;

    // The command pool and image belong to one queue family. Reject a changed
    // submit queue rather than silently submitting a command buffer created
    // for a different family or racing transfers from separate queues.
    if (!queue || (upload_queue_ && (queue != upload_queue_ || family != upload_family_)))
        return false;
    upload_queue_ = queue;
    upload_family_ = family;

    // A fence protects reuse of the transfer command pool/staging buffer.
    // Do not stall all queues or wait for this frame's transfer completion:
    // the subsequent ImGui draw is submitted to the same graphics queue,
    // and Vulkan ordering/barriers make the uploaded image visible to it.
    if (upload_in_flight_) {
        const auto wait = vkWaitForFences(device_, 1, &upload_fence_, VK_TRUE, 2000000000ULL);
        if (wait != VK_SUCCESS) {
            std::fprintf(stderr, "MirrorPSX Vulkan: upload fence wait failed: %d\n", int(wait));
            return false;
        }
        upload_in_flight_ = false;
    }
    const auto bytes = VkDeviceSize(frame.rgba.size());
    if (!prepare_upload(bytes, family)) return false;
    if (width_ != frame.width || height_ != frame.height) {
        // The *previous display draw* may still be sampling the old image.
        // Replacing descriptors at a resolution change requires full quiescence.
        if (vkDeviceWaitIdle(device_) != VK_SUCCESS) return false;
        destroy_image();
        VkFormatProperties format{};
        vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_R8G8B8A8_UNORM, &format);
        constexpr VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((format.optimalTilingFeatures & required) != required) return false;

        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent = {uint32_t(frame.width), uint32_t(frame.height), 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &ii, nullptr, &image_) != VK_SUCCESS) return false;
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, image_, &req);
        auto type = find_memory(physical_, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type == UINT32_MAX) type = find_memory(physical_, req.memoryTypeBits, 0);
        if (type == UINT32_MAX) return false;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = type;
        if (vkAllocateMemory(device_, &ai, nullptr, &memory_) != VK_SUCCESS ||
            vkBindImageMemory(device_, image_, memory_, 0) != VK_SUCCESS) return false;

        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = image_;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ii.format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &vi, nullptr, &view_) != VK_SUCCESS) return false;

        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 1;
        if (vkCreateSampler(device_, &si, nullptr, &sampler_) != VK_SUCCESS) return false;
        descriptor_ = ImGui_ImplVulkan_AddTexture(sampler_, view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!descriptor_) return false;
        width_ = frame.width;
        height_ = frame.height;
    }
    void* pixels = nullptr;
    if (vkMapMemory(device_, upload_memory_, 0, bytes, 0, &pixels) != VK_SUCCESS) return false;
    std::memcpy(pixels, frame.rgba.data(), size_t(bytes));
    vkUnmapMemory(device_, upload_memory_);

    if (vkResetCommandPool(device_, command_pool_, 0) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command_, &begin) != VK_SUCCESS) return false;

    VkImageMemoryBarrier into_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    into_transfer.srcAccessMask = initialized_ ? VK_ACCESS_SHADER_READ_BIT : 0;
    into_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    into_transfer.oldLayout = initialized_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                            : VK_IMAGE_LAYOUT_UNDEFINED;
    into_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    into_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    into_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    into_transfer.image = image_;
    into_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    into_transfer.subresourceRange.levelCount = 1;
    into_transfer.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command_, initialized_ ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &into_transfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {uint32_t(width_), uint32_t(height_), 1};
    vkCmdCopyBufferToImage(command_, upload_buffer_, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copy);

    VkImageMemoryBarrier into_shader{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    into_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    into_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    into_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    into_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    into_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    into_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    into_shader.image = image_;
    into_shader.subresourceRange = into_transfer.subresourceRange;
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &into_shader);
    if (vkEndCommandBuffer(command_) != VK_SUCCESS) return false;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_;
    if (vkResetFences(device_, 1, &upload_fence_) != VK_SUCCESS) return false;
    if (vkQueueSubmit(queue, 1, &submit, upload_fence_) != VK_SUCCESS) return false;
    upload_in_flight_ = true;
    initialized_ = true;
    return true;
}
}
