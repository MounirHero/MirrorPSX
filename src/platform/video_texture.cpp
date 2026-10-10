// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/video_texture.hpp"
#include "imgui_impl_vulkan.h"
#include <cstring>

namespace mirror {
namespace {
uint32_t memory_type(VkPhysicalDevice physical,uint32_t bits,VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical,&props);
    for(uint32_t i=0;i<props.memoryTypeCount;++i)
        if((bits & (1u<<i)) && (props.memoryTypes[i].propertyFlags & flags)==flags) return i;
    return UINT32_MAX;
}
}
void VideoTexture::destroy_image() {
    if(descriptor_) { ImGui_ImplVulkan_RemoveTexture(descriptor_); descriptor_=VK_NULL_HANDLE; }
    if(sampler_) vkDestroySampler(device_,sampler_,nullptr);
    if(view_) vkDestroyImageView(device_,view_,nullptr);
    if(image_) vkDestroyImage(device_,image_,nullptr);
    if(memory_) vkFreeMemory(device_,memory_,nullptr);
    sampler_=VK_NULL_HANDLE;view_=VK_NULL_HANDLE;image_=VK_NULL_HANDLE;memory_=VK_NULL_HANDLE;
    width_=height_=0;
    initialized_=false;
}
VideoTexture::~VideoTexture(){ destroy_image(); }
bool VideoTexture::upload(VkQueue queue,uint32_t family,const VideoFrame& frame) {
    if(frame.width<=0 || frame.height<=0 || frame.width>4096 || frame.height>4096 ||
       frame.rgba.size()!=size_t(frame.width)*size_t(frame.height)*4) return false;
    // The application synchronizes uploads with the frame loop. Waiting also protects
    // the previous descriptor and image from pending GPU draws during resize.
    if(vkDeviceWaitIdle(device_)!=VK_SUCCESS) return false;
    if(width_!=frame.width || height_!=frame.height) {
        destroy_image();
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(physical_,VK_FORMAT_R8G8B8A8_UNORM,&format_properties);
        if(!(format_properties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return false;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType=VK_IMAGE_TYPE_2D;info.format=VK_FORMAT_R8G8B8A8_UNORM;
        info.extent={uint32_t(frame.width),uint32_t(frame.height),1};info.mipLevels=1;
        info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_LINEAR;
        info.usage=VK_IMAGE_USAGE_SAMPLED_BIT;info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        if(vkCreateImage(device_,&info,nullptr,&image_)!=VK_SUCCESS) {destroy_image();return false;}
        VkMemoryRequirements req{};vkGetImageMemoryRequirements(device_,image_,&req);
        auto type=memory_type(physical_,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(type==UINT32_MAX) {destroy_image();return false;}
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
        if(vkAllocateMemory(device_,&alloc,nullptr,&memory_)!=VK_SUCCESS ||
           vkBindImageMemory(device_,image_,memory_,0)!=VK_SUCCESS) {destroy_image();return false;}
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=image_;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=info.format;
        vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;vi.subresourceRange.levelCount=1;vi.subresourceRange.layerCount=1;
        if(vkCreateImageView(device_,&vi,nullptr,&view_)!=VK_SUCCESS) {destroy_image();return false;}
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter=si.minFilter=VK_FILTER_LINEAR;
        si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod=1;
        if(vkCreateSampler(device_,&si,nullptr,&sampler_)!=VK_SUCCESS) {destroy_image();return false;}
        descriptor_=ImGui_ImplVulkan_AddTexture(sampler_,view_,VK_IMAGE_LAYOUT_GENERAL);
        width_=frame.width;height_=frame.height;
    }
    VkImageSubresource sub{VK_IMAGE_ASPECT_COLOR_BIT,0,0};
    VkSubresourceLayout layout{};vkGetImageSubresourceLayout(device_,image_,&sub,&layout);
    void* mapped=nullptr;
    if(vkMapMemory(device_,memory_,0,VK_WHOLE_SIZE,0,&mapped)!=VK_SUCCESS) return false;
    for(int y=0;y<height_;++y)
        std::memcpy(static_cast<uint8_t*>(mapped)+layout.offset+size_t(y)*layout.rowPitch,
                    frame.rgba.data()+size_t(y)*width_*4,size_t(width_)*4);
    vkUnmapMemory(device_,memory_);
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if(vkCreateCommandPool(device_,&pi,nullptr,&pool)!=VK_SUCCESS) return false;
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    bool ok=vkAllocateCommandBuffers(device_,&ai,&cmd)==VK_SUCCESS;
    if(ok) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok=vkBeginCommandBuffer(cmd,&begin)==VK_SUCCESS;
    }
    if(ok) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout=initialized_?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=image_;barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount=1;barrier.subresourceRange.layerCount=1;
        barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,0,nullptr,0,nullptr,1,&barrier);
        ok=vkEndCommandBuffer(cmd)==VK_SUCCESS;
    }
    if(ok) {
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        ok=vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)==VK_SUCCESS;
        if(ok) ok=vkQueueWaitIdle(queue)==VK_SUCCESS;
    }
    vkDestroyCommandPool(device_,pool,nullptr);
    if(ok) initialized_=true;
    return ok;
}
}
