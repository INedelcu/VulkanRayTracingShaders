#include "GpuTimer.h"

bool GpuTimer::Create(uint32_t frameCount, const char* name)
{
    const PhysicalGfxDeviceInfo& physicalDevice = g_VkPhysicalDevices[g_CurrentPhysicalDevice];
    const uint32_t validBits = physicalDevice.deviceQueueFamilyProperties[g_VkQueueFamilyIndex].timestampValidBits;

    if (validBits == 0)
        return false;

    m_TickNanoseconds = physicalDevice.deviceProperties.properties.limits.timestampPeriod;
    m_TickMask = validBits >= 64 ? UINT64_MAX : (1ull << validBits) - 1;
    m_Pending.assign(frameCount, false);

    VkQueryPoolCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    createInfo.queryCount = frameCount * 2;

    if (vkCreateQueryPool(g_VkDevice, &createInfo, nullptr, &m_Pool) != VK_SUCCESS)
    {
        m_Pool = VK_NULL_HANDLE;
        return false;
    }

    SetVkObjectName((uint64_t)m_Pool, VK_OBJECT_TYPE_QUERY_POOL, name);

    return true;
}

void GpuTimer::Destroy()
{
    if (m_Pool != VK_NULL_HANDLE)
        vkDestroyQueryPool(g_VkDevice, m_Pool, nullptr);

    m_Pool = VK_NULL_HANDLE;
    m_Pending.clear();
}

void GpuTimer::Begin(VkCommandBuffer commandBuffer, uint32_t frame)
{
    if (m_Pool == VK_NULL_HANDLE)
        return;

    vkCmdResetQueryPool(commandBuffer, m_Pool, frame * 2, 2);
    vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_Pool, frame * 2);
}

void GpuTimer::End(VkCommandBuffer commandBuffer, uint32_t frame)
{
    if (m_Pool == VK_NULL_HANDLE)
        return;

    vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_Pool, frame * 2 + 1);
    m_Pending[frame] = true;
}

bool GpuTimer::Read(uint32_t frame, double& milliseconds)
{
    if (m_Pool == VK_NULL_HANDLE || !m_Pending[frame])
        return false;

    uint64_t ticks[2];

    if (vkGetQueryPoolResults(g_VkDevice, m_Pool, frame * 2, 2, sizeof(ticks), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return false;

    m_Pending[frame] = false;
    milliseconds = ((ticks[1] - ticks[0]) & m_TickMask) * m_TickNanoseconds * 1e-6;

    return true;
}
