// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/video_texture.hpp"
#include "imgui_impl_vulkan.h"
#include <cstring>
#include <stdexcept>
namespace mirror {
static void check(VkResult r) {
  if (r != VK_SUCCESS)
    throw std::runtime_error("Vulkan video upload failed: " +
                             std::to_string(r));
}
uint32_t VideoTexture::memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties p;
  vkGetPhysicalDeviceMemoryProperties(physical_, &p);
  for (uint32_t i = 0; i < p.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  throw std::runtime_error("Vulkan memory type unavailable");
}
void VideoTexture::release() {
  vkDeviceWaitIdle(device_);
  if (descriptor_)
    ImGui_ImplVulkan_RemoveTexture(descriptor_);
  if (sampler_)
    vkDestroySampler(device_, sampler_, nullptr);
  if (view_)
    vkDestroyImageView(device_, view_, nullptr);
  if (image_)
    vkDestroyImage(device_, image_, nullptr);
  if (memory_)
    vkFreeMemory(device_, memory_, nullptr);
  descriptor_ = VK_NULL_HANDLE;
  sampler_ = VK_NULL_HANDLE;
  view_ = VK_NULL_HANDLE;
  image_ = VK_NULL_HANDLE;
  memory_ = VK_NULL_HANDLE;
}
VideoTexture::~VideoTexture() {
  release();
  if (mapped_)
    vkUnmapMemory(device_, staging_memory_);
  if (staging_buffer_)
    vkDestroyBuffer(device_, staging_buffer_, nullptr);
  if (staging_memory_)
    vkFreeMemory(device_, staging_memory_, nullptr);
  if (upload_fence_)
    vkDestroyFence(device_, upload_fence_, nullptr);
  if (pool_)
    vkDestroyCommandPool(device_, pool_, nullptr);
}
void VideoTexture::staging(VkDeviceSize bytes) {
  if (bytes <= staging_capacity_)
    return;
  if (mapped_)
    vkUnmapMemory(device_, staging_memory_);
  mapped_ = nullptr;
  if (staging_buffer_)
    vkDestroyBuffer(device_, staging_buffer_, nullptr);
  if (staging_memory_)
    vkFreeMemory(device_, staging_memory_, nullptr);
  staging_buffer_ = VK_NULL_HANDLE;
  staging_memory_ = VK_NULL_HANDLE;
  staging_capacity_ = 0;
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = bytes;
  bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  check(vkCreateBuffer(device_, &bi, nullptr, &staging_buffer_));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(device_, staging_buffer_, &req);
  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex =
      memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  check(vkAllocateMemory(device_, &alloc, nullptr, &staging_memory_));
  check(vkBindBufferMemory(device_, staging_buffer_, staging_memory_, 0));
  check(vkMapMemory(device_, staging_memory_, 0, bytes, 0, &mapped_));
  staging_capacity_ = bytes;
}
void VideoTexture::update(const State &state) {
  auto status = state.snapshot();
  if (!status.connected) {
    if (image_)
      release();
    sequence_ = status.frames;
    return;
  }
  if (upload_fence_) {
    auto result = vkGetFenceStatus(device_, upload_fence_);
    if (result == VK_NOT_READY)
      return; // Keep only the latest mailbox frame while the upload is busy.
    check(result);
  }
  auto current = state.latest_frame(sequence_);
  if (!current)
    return;
  const auto &frame = *current;
  if (frame.width <= 0 || frame.height <= 0 ||
      frame.rgba.size() != size_t(frame.width) * frame.height * 4)
    return;
  bool fresh = frame.width != width_ || frame.height != height_ || !image_;
  if (fresh) {
    release();
    width_ = frame.width;
    height_ = frame.height;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = {uint32_t(width_), uint32_t(height_), 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    check(vkCreateImage(device_, &ci, nullptr, &image_));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, image_, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex =
        memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_, &alloc, nullptr, &memory_));
    check(vkBindImageMemory(device_, image_, memory_, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ci.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(device_, &vi, nullptr, &view_));
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 1;
    check(vkCreateSampler(device_, &si, nullptr, &sampler_));
    descriptor_ = ImGui_ImplVulkan_AddTexture(
        sampler_, view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  if (!pool_) {
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = family_;
    pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    check(vkCreateCommandPool(device_, &pi, nullptr, &pool_));
  }
  if (!command_) {
    VkCommandBufferAllocateInfo ai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &ai, &command_));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(device_, &fi, nullptr, &upload_fence_));
  }
  staging(frame.rgba.size());
  std::memcpy(mapped_, frame.rgba.data(), frame.rgba.size());
  check(vkResetCommandPool(device_, pool_, 0));
  VkCommandBuffer cmd = command_;
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(cmd, &begin));
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = fresh ? VK_IMAGE_LAYOUT_UNDEFINED
                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcAccessMask = fresh ? 0 : VK_ACCESS_SHADER_READ_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
      VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image_;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd,
                       fresh ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                             : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {uint32_t(width_), uint32_t(height_), 1};
  vkCmdCopyBufferToImage(cmd, staging_buffer_, image_,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  barrier.oldLayout = barrier.newLayout;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  check(vkEndCommandBuffer(cmd));
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  check(vkResetFences(device_, 1, &upload_fence_));
  // Upload and UI rendering use the same queue. Barriers order prior sampling,
  // transfer writes and subsequent sampling without stalling the CPU.
  check(vkQueueSubmit(queue_, 1, &submit, upload_fence_));
  sequence_ = frame.sequence;
}
} // namespace mirror
