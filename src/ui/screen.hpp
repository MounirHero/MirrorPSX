// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver/state.hpp"
#include <vulkan/vulkan.h>
namespace mirror {
class AirPlay;
void render_screen(AirPlay& receiver, const Snapshot& status, VkDescriptorSet video, int video_width, int video_height);
}
