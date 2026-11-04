#pragma once

#include "VulkanDevice.h"

#include <vector>

// GPU time between Begin and End, measured with one timestamp query pair per frame in flight.
class GpuTimer
{
public:
    bool Create(uint32_t frameCount, const char* name);
    void Destroy();

    void Begin(VkCommandBuffer commandBuffer, uint32_t frame);
    void End(VkCommandBuffer commandBuffer, uint32_t frame);

    // Call after the frame's fence has signaled. Returns false when there is no new result for the frame.
    bool Read(uint32_t frame, double& milliseconds);

private:
    VkQueryPool       m_Pool = VK_NULL_HANDLE;
    double            m_TickNanoseconds = 0.0;
    uint64_t          m_TickMask = 0;
    std::vector<bool> m_Pending;
};
