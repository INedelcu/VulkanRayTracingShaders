#include "DescriptorHeap.h"

#include <algorithm>

bool DescriptorHeap::Create(Type type, uint32_t capacity, const char* name)
{
    const VkPhysicalDeviceDescriptorHeapPropertiesEXT& props = g_VkPhysicalDevices[g_CurrentPhysicalDevice].deviceDescriptorHeapProperties;

    m_Type = type;
    m_Capacity = capacity;
    m_Count = 0;

    VkDeviceSize heapAlignment;
    VkDeviceSize maxHeapSize;
    VkDeviceSize slotAlignment;

    if (type == Type::Resource)
    {
        slotAlignment = std::max<VkDeviceSize>(props.imageDescriptorAlignment, props.bufferDescriptorAlignment);
        m_SlotSize = AlignUp(std::max<VkDeviceSize>(props.imageDescriptorSize, props.bufferDescriptorSize), slotAlignment);
        m_ReservedRangeSize = props.minResourceHeapReservedRange;
        heapAlignment = props.resourceHeapAlignment;
        maxHeapSize = props.maxResourceHeapSize;
    }
    else
    {
        slotAlignment = props.samplerDescriptorAlignment;
        m_SlotSize = AlignUp(props.samplerDescriptorSize, slotAlignment);
        m_ReservedRangeSize = props.minSamplerHeapReservedRange;
        heapAlignment = props.samplerHeapAlignment;
        maxHeapSize = props.maxSamplerHeapSize;
    }

    m_ReservedRangeOffset = AlignUp(capacity * m_SlotSize, slotAlignment);
    const VkDeviceSize heapSize = m_ReservedRangeOffset + m_ReservedRangeSize;

    if (heapSize > maxHeapSize)
        return false;

    m_Buffer = CreateBuffer(heapSize, VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, name);

    if (m_Buffer.buffer == VK_NULL_HANDLE || m_Buffer.mapped == nullptr || m_Buffer.address % heapAlignment != 0)
    {
        DestroyBuffer(m_Buffer);
        return false;
    }

    return true;
}

void DescriptorHeap::Destroy()
{
    DestroyBuffer(m_Buffer);

    m_Capacity = 0;
    m_Count = 0;
}

uint32_t DescriptorHeap::Allocate()
{
    if (m_Count >= m_Capacity)
        return UINT32_MAX;

    return m_Count++;
}

VkHostAddressRangeEXT DescriptorHeap::SlotRange(uint32_t slot)
{
    VkHostAddressRangeEXT range;
    range.address = (uint8_t*)m_Buffer.mapped + SlotOffset(slot);
    range.size = (size_t)m_SlotSize;

    return range;
}

bool DescriptorHeap::WriteImage(uint32_t slot, const Image& image, VkDescriptorType type, VkImageLayout layout)
{
    if (slot >= m_Count)
        return false;

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = image.format;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, image.mipLevels, 0, 1 };

    VkImageDescriptorInfoEXT imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
    imageInfo.pView = &viewInfo;
    imageInfo.layout = layout;

    VkResourceDescriptorInfoEXT resource = {};
    resource.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    resource.type = type;
    resource.data.pImage = &imageInfo;

    VkHostAddressRangeEXT destination = SlotRange(slot);

    return vkWriteResourceDescriptorsEXT(g_VkDevice, 1, &resource, &destination) == VK_SUCCESS;
}

bool DescriptorHeap::WriteSampledImage(uint32_t slot, const Image& image)
{
    return WriteImage(slot, image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

bool DescriptorHeap::WriteStorageImage(uint32_t slot, const Image& image)
{
    return WriteImage(slot, image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
}

bool DescriptorHeap::WriteSampler(uint32_t slot, const VkSamplerCreateInfo& samplerInfo)
{
    if (slot >= m_Count)
        return false;

    VkHostAddressRangeEXT destination = SlotRange(slot);

    return vkWriteSamplerDescriptorsEXT(g_VkDevice, 1, &samplerInfo, &destination) == VK_SUCCESS;
}

void DescriptorHeap::Bind(VkCommandBuffer commandBuffer) const
{
    VkBindHeapInfoEXT bindInfo = {};
    bindInfo.sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT;
    bindInfo.heapRange.address = m_Buffer.address;
    bindInfo.heapRange.size = m_Buffer.size;
    bindInfo.reservedRangeOffset = m_ReservedRangeOffset;
    bindInfo.reservedRangeSize = m_ReservedRangeSize;

    if (m_Type == Type::Resource)
        vkCmdBindResourceHeapEXT(commandBuffer, &bindInfo);
    else
        vkCmdBindSamplerHeapEXT(commandBuffer, &bindInfo);
}
