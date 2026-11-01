#include "Renderer.h"
#include "VulkanDevice.h"
#include "RayTracingScene.h"

size_t		g_CurrentFrame = 0;
uint32_t    g_FrameIndex = 0;

bool InitializeRenderer(HINSTANCE hInstance, HWND hWnd)
{
    if (!InitializeVulkan(hInstance, hWnd))
        return false;

#ifdef VK_KHR_acceleration_structure
    CreateRayTracingScene();
#endif

    return true;
}

void ShutdownRenderer()
{
    if (g_VkDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(g_VkDevice);
    }

#ifdef VK_KHR_acceleration_structure
    DestroyRayTracingScene();
#endif

    ShutdownVulkan();
}

void Render()
{
    if (g_VkSwapChain == VK_NULL_HANDLE && !RecreateSwapChain())
        return;

    vkWaitForFences(g_VkDevice, 1, &g_VkFrameFence[g_CurrentFrame], VK_TRUE, (uint64_t)-1);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(g_VkDevice, g_VkSwapChain, (uint64_t)-1, g_VkImageAvailableSemaphore[g_CurrentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapChain();
        return;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        return;

    vkResetFences(g_VkDevice, 1, &g_VkFrameFence[g_CurrentFrame]);

    VkImage swapChainImage = g_VkSwapChainImages[imageIndex];

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    VkCommandBuffer cmdBuffer = g_VkCommandBuffers[g_CurrentFrame];

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

#ifdef VK_KHR_acceleration_structure
    BuildTLAS(cmdBuffer, g_CurrentFrame);
#endif

    VkImageMemoryBarrier imageMemoryBarrier = {};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageMemoryBarrier.image = swapChainImage;
    imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
    imageMemoryBarrier.subresourceRange.levelCount = 1;
    imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
    imageMemoryBarrier.subresourceRange.layerCount = 1;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.srcAccessMask = 0;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

    VkClearColorValue clearColor;
    clearColor.float32[0] = 0.85f;
    clearColor.float32[1] = 0.05f;
    clearColor.float32[2] = 0.2f;
    clearColor.float32[3] = 1.0f;

    VkImageSubresourceRange range;
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseArrayLayer = 0;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.layerCount = 1;

    vkCmdClearColorImage(cmdBuffer, swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

    vkEndCommandBuffer(cmdBuffer);

    VkSemaphore waitSemaphores[] = { g_VkImageAvailableSemaphore[g_CurrentFrame] };
    VkSemaphore signalSemaphores[] = { g_VkRenderingFinishedSemaphore[imageIndex] };

    const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitDstStageMask = &waitDstStageMask;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(g_VkQueue, 1, &submitInfo, g_VkFrameFence[g_CurrentFrame]);;

    // Present
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &g_VkRenderingFinishedSemaphore[imageIndex];
    presentInfo.pSwapchains = &g_VkSwapChain;
    presentInfo.swapchainCount = 1;
    presentInfo.pResults = nullptr;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(g_VkQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        RecreateSwapChain();

    g_CurrentFrame = (g_CurrentFrame + 1) % g_MaxFramesInFlight;

    g_FrameIndex++;
}
