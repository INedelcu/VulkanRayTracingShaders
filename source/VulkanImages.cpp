#include "VulkanImages.h"
#include "VulkanBuffers.h"

Image CreateImage(VkExtent2D extent, VkFormat format, uint32_t mipLevels, VkImageUsageFlags usage, const char* name)
{
    Image result;
    result.format = format;
    result.extent = extent;
    result.mipLevels = mipLevels;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { extent.width, extent.height, 1 };
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(g_VkDevice, &imageInfo, nullptr, &result.image) != VK_SUCCESS)
        return {};

    SetVkObjectName((uint64_t)result.image, VK_OBJECT_TYPE_IMAGE, name);

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_VkDevice, result.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(g_VkDevice, &allocInfo, nullptr, &result.memory) != VK_SUCCESS)
    {
        vkDestroyImage(g_VkDevice, result.image, nullptr);
        return {};
    }

    vkBindImageMemory(g_VkDevice, result.image, result.memory, 0);

    return result;
}

void DestroyImage(Image& image)
{
    if (image.image != VK_NULL_HANDLE)
        vkDestroyImage(g_VkDevice, image.image, nullptr);

    if (image.memory != VK_NULL_HANDLE)
        vkFreeMemory(g_VkDevice, image.memory, nullptr);

    image = {};
}

void TransitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                     uint32_t mipLevels)
{
    VkImageMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };

    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}
