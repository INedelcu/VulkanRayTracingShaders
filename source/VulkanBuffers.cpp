#include "VulkanBuffers.h"

#include <cstring>

uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties = g_VkPhysicalDevices[g_CurrentPhysicalDevice].deviceMemoryProperties;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    return 0;
}

Buffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties, const char* name)
{
    Buffer result;
    result.size = size;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(g_VkDevice, &bufferInfo, nullptr, &result.buffer) != VK_SUCCESS)
        return {};

    SetVkObjectName((uint64_t)result.buffer, VK_OBJECT_TYPE_BUFFER, name);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_VkDevice, result.buffer, &memRequirements);

    VkMemoryAllocateFlagsInfo allocFlagsInfo = {};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? (void*)&allocFlagsInfo : nullptr;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, memoryProperties);

    if (vkAllocateMemory(g_VkDevice, &allocInfo, nullptr, &result.memory) != VK_SUCCESS)
    {
        vkDestroyBuffer(g_VkDevice, result.buffer, nullptr);
        return {};
    }

    vkBindBufferMemory(g_VkDevice, result.buffer, result.memory, 0);

    if (memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        vkMapMemory(g_VkDevice, result.memory, 0, VK_WHOLE_SIZE, 0, &result.mapped);

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        VkBufferDeviceAddressInfo addressInfo = {};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = result.buffer;

        result.address = vkGetBufferDeviceAddress(g_VkDevice, &addressInfo);
    }

    return result;
}

Buffer CreateBufferWithData(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* name)
{
    Buffer staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "Staging");
    memcpy(staging.mapped, data, (size_t)size);

    Buffer result = CreateBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, name);

    VkCommandBuffer commandBuffer = BeginOneTimeCommands();

    VkBufferCopy copyRegion = {};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, staging.buffer, result.buffer, 1, &copyRegion);

    VkMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

    EndOneTimeCommands(commandBuffer);

    DestroyBuffer(staging);

    return result;
}

void DestroyBuffer(Buffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(g_VkDevice, buffer.buffer, nullptr);

    if (buffer.memory != VK_NULL_HANDLE)
        vkFreeMemory(g_VkDevice, buffer.memory, nullptr);

    buffer = {};
}
