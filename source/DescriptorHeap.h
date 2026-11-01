#pragma once

#include "VulkanBuffers.h"
#include "VulkanImages.h"

class DescriptorHeap
{
public:
    enum class Type
    {
        Resource,
        Sampler,
    };

    bool Create(Type type, uint32_t capacity, const char* name);
    void Destroy();

    uint32_t     Allocate();

    VkDeviceSize SlotSize() const { return m_SlotSize; }
    VkDeviceSize SlotOffset(uint32_t slot) const { return slot * m_SlotSize; }

    bool WriteSampledImage(uint32_t slot, const Image& image);
    bool WriteStorageImage(uint32_t slot, const Image& image);
    bool WriteSampler(uint32_t slot, const VkSamplerCreateInfo& samplerInfo);

    void Bind(VkCommandBuffer commandBuffer) const;

private:
    bool WriteImage(uint32_t slot, const Image& image, VkDescriptorType type, VkImageLayout layout);
    VkHostAddressRangeEXT SlotRange(uint32_t slot);

    Type         m_Type = Type::Resource;
    Buffer       m_Buffer;
    uint32_t     m_Capacity = 0;
    uint32_t     m_Count = 0;
    VkDeviceSize m_SlotSize = 0;
    VkDeviceSize m_ReservedRangeOffset = 0;
    VkDeviceSize m_ReservedRangeSize = 0;
};
