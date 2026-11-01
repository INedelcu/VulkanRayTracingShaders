#include "Renderer.h"
#include "VulkanDevice.h"
#include "RayTracingScene.h"
#include "Shaders.h"
#include "ShaderReflection.h"
#include "VulkanImages.h"

size_t		g_CurrentFrame = 0;
uint32_t    g_FrameIndex = 0;

VkShaderModule g_RayGenShader = VK_NULL_HANDLE;
Image          g_OutputImage;

bool           g_RendererReady = false;
bool           g_WindowResized = false;

static bool UpdateOutputImage()
{
    if (g_OutputImage.extent.width == g_VkSwapChainExtent.width && g_OutputImage.extent.height == g_VkSwapChainExtent.height)
        return true;

    if (g_OutputImage.image != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(g_VkDevice);
        DestroyImage(g_OutputImage);
    }

    g_OutputImage = CreateImage(g_VkSwapChainExtent, VK_FORMAT_R32G32B32A32_SFLOAT, 1, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "Output");

    return g_OutputImage.image != VK_NULL_HANDLE;
}

bool InitializeRenderer(HINSTANCE hInstance, HWND hWnd)
{
    if (!InitializeVulkan(hInstance, hWnd))
        return false;

#ifdef VK_KHR_acceleration_structure
    CreateRayTracingScene();
#endif

    if (!UpdateOutputImage())
    {
        MessageBox(hWnd, "Couldn't create the output image. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    const std::vector<uint32_t> rayGenSpirv = LoadSpirv("RayGen");

    if (!rayGenSpirv.empty())
        g_RayGenShader = CreateShaderModule(rayGenSpirv, "RayGen");

    if (g_RayGenShader == VK_NULL_HANDLE)
    {
        MessageBox(hWnd, "Couldn't load shaders\\RayGen.spv. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    const ShaderReflection rayGenReflection(rayGenSpirv);

    for (const ShaderResourceBinding& binding : rayGenReflection.Bindings())
    {
        char message[256];
        _snprintf_s(message, _TRUNCATE, "RayGen.spv: %s = set %u, binding %u\n", binding.name.c_str(), binding.set, binding.binding);
        OutputDebugString(message);
    }

    g_RendererReady = true;

    return true;
}

void OnWindowResized(uint32_t width, uint32_t height)
{
    if (g_RendererReady && (width != g_VkSwapChainExtent.width || height != g_VkSwapChainExtent.height))
        g_WindowResized = true;
}

void ShutdownRenderer()
{
    g_RendererReady = false;

    if (g_VkDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(g_VkDevice);
    }

    if (g_RayGenShader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(g_VkDevice, g_RayGenShader, nullptr);
        g_RayGenShader = VK_NULL_HANDLE;
    }

    DestroyImage(g_OutputImage);

#ifdef VK_KHR_acceleration_structure
    DestroyRayTracingScene();
#endif

    ShutdownVulkan();
}

static VkCommandBuffer BeginFrame(uint32_t& imageIndex)
{
    if (g_VkSwapChain == VK_NULL_HANDLE && !RecreateSwapChain())
        return VK_NULL_HANDLE;

    vkWaitForFences(g_VkDevice, 1, &g_VkFrameFence[g_CurrentFrame], VK_TRUE, (uint64_t)-1);

    if (g_WindowResized)
    {
        if (!RecreateSwapChain())
            return VK_NULL_HANDLE;

        g_WindowResized = false;
    }

    if (!UpdateOutputImage())
        return VK_NULL_HANDLE;

    VkResult result = vkAcquireNextImageKHR(g_VkDevice, g_VkSwapChain, (uint64_t)-1, g_VkImageAvailableSemaphore[g_CurrentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapChain();
        return VK_NULL_HANDLE;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        return VK_NULL_HANDLE;

    vkResetFences(g_VkDevice, 1, &g_VkFrameFence[g_CurrentFrame]);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    VkCommandBuffer cmdBuffer = g_VkCommandBuffers[g_CurrentFrame];

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    return cmdBuffer;
}

static void RenderFrame(VkCommandBuffer cmdBuffer)
{
#ifdef VK_KHR_acceleration_structure
    BuildTLAS(cmdBuffer, g_CurrentFrame);
#endif

	TransitionImage(cmdBuffer, g_OutputImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

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

    vkCmdClearColorImage(cmdBuffer, g_OutputImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

    TransitionImage(cmdBuffer, g_OutputImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
}

static void EndFrame(VkCommandBuffer cmdBuffer, uint32_t imageIndex)
{
    VkImage swapChainImage = g_VkSwapChainImages[imageIndex];

    TransitionImage(cmdBuffer, swapChainImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkImageBlit blit = {};
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.srcOffsets[1] = { (int32_t)g_OutputImage.extent.width, (int32_t)g_OutputImage.extent.height, 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstOffsets[1] = { (int32_t)g_VkSwapChainExtent.width, (int32_t)g_VkSwapChainExtent.height, 1 };

    vkCmdBlitImage(cmdBuffer, g_OutputImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

    TransitionImage(cmdBuffer, swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);

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

    vkQueueSubmit(g_VkQueue, 1, &submitInfo, g_VkFrameFence[g_CurrentFrame]);

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

    VkResult result = vkQueuePresentKHR(g_VkQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        RecreateSwapChain();

    g_CurrentFrame = (g_CurrentFrame + 1) % g_MaxFramesInFlight;

    g_FrameIndex++;
}

void Render()
{
    if (!g_RendererReady)
        return;

    uint32_t imageIndex = 0;
    VkCommandBuffer cmdBuffer = BeginFrame(imageIndex);

    if (cmdBuffer == VK_NULL_HANDLE)
        return;

    RenderFrame(cmdBuffer);

    EndFrame(cmdBuffer, imageIndex);
}
