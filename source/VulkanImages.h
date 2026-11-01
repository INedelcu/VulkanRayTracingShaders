#pragma once

#include "VulkanDevice.h"

struct Image
{
    VkImage        image     = VK_NULL_HANDLE;
    VkDeviceMemory memory    = VK_NULL_HANDLE;
    VkFormat       format    = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent    = {};
    uint32_t       mipLevels = 1;
};

Image CreateImage(VkExtent2D extent, VkFormat format, uint32_t mipLevels, VkImageUsageFlags usage, const char* name);
void DestroyImage(Image& image);

void TransitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                     uint32_t mipLevels = VK_REMAINING_MIP_LEVELS);
