#pragma once

#include "VulkanDevice.h"

struct Buffer
{
    VkBuffer        buffer  = VK_NULL_HANDLE;
    VkDeviceMemory  memory  = VK_NULL_HANDLE;
    VkDeviceSize    size    = 0;
    VkDeviceAddress address = 0; // valid when created with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    void*           mapped  = nullptr;
};

inline VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
Buffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties, const char* name);
Buffer CreateBufferWithData(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* name);
void DestroyBuffer(Buffer& buffer);
