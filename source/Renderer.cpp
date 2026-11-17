#include "Renderer.h"

#include "Camera.h"
#include "DescriptorHeap.h"
#include "GpuTimer.h"
#include "RayTracingPipeline.h"
#include "RayTracingScene.h"
#include "RayTracingShader.h"
#include "VulkanDevice.h"
#include "VulkanImages.h"

#include <algorithm>
#include <cmath>

struct PushData
{
    shared::FrameConstants frame;
    VkDeviceAddress        accelStruct;
};

static_assert(offsetof(PushData, accelStruct) % 8 == 0, "push addresses must be 8-byte aligned");

size_t      g_CurrentFrame = 0;
uint32_t    g_FrameIndex = 0;
uint32_t    g_AccumulatedFrames = 0;

DescriptorHeap     g_ResourceHeap;
DescriptorHeap     g_SamplerHeap;
Image              g_AccumulationImage;
Image              g_OutputImage;
uint32_t           g_AccumulationImageSlot = UINT32_MAX;
uint32_t           g_OutputImageSlot = UINT32_MAX;
uint32_t           g_LinearSamplerSlot = UINT32_MAX;
RayTracingGlobalResources g_GlobalResources;
RayTracingRayGenShader    g_RayGenShader;
RayTracingHitRecords      g_HitRecords;
RayTracingPipeline        g_RayTracingPipeline;
OrbitCamera        g_Camera;
GpuTimer           g_TraceTimer;

bool           g_RendererReady = false;
bool           g_WindowResized = false;
uint64_t       g_PresentId = 0;

HWND           g_Window = nullptr;
char           g_WindowTitle[128] = {};
double         g_TraceTimeSum = 0.0;
uint32_t       g_TraceTimeCount = 0;
ULONGLONG      g_TraceTimeReportTick = 0;

static bool RecreateSwapChainAndResetPresentId()
{
    g_PresentId = 0;

    return RecreateSwapChain();
}

static void ReportTraceTime(double milliseconds)
{
    g_TraceTimeSum += milliseconds;
    g_TraceTimeCount++;

    const ULONGLONG now = GetTickCount64();

    if (now - g_TraceTimeReportTick < 500)
        return;

    char title[256];
    _snprintf_s(title, _TRUNCATE, "%s - vkCmdTraceRaysKHR: %.3f ms", g_WindowTitle, g_TraceTimeSum / g_TraceTimeCount);
    SetWindowText(g_Window, title);

    g_TraceTimeSum = 0.0;
    g_TraceTimeCount = 0;
    g_TraceTimeReportTick = now;
}

static bool CreateRenderTarget(Image& image, uint32_t slot, VkFormat format, VkImageUsageFlags usage, const char* name)
{
    image = CreateImage(g_VkSwapChainExtent, format, 1, usage, name);

    if (image.image == VK_NULL_HANDLE)
        return false;

    if (!g_ResourceHeap.WriteStorageImage(slot, image))
    {
        DestroyImage(image);
        return false;
    }

    return true;
}

static bool UpdateRenderTargets()
{
    if (g_OutputImage.extent.width == g_VkSwapChainExtent.width && g_OutputImage.extent.height == g_VkSwapChainExtent.height)
        return true;

    g_AccumulatedFrames = 0;

    if (g_OutputImage.image != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(g_VkDevice);
        DestroyImage(g_AccumulationImage);
        DestroyImage(g_OutputImage);
    }

    return CreateRenderTarget(g_AccumulationImage, g_AccumulationImageSlot, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, "Accumulation") &&
           CreateRenderTarget(g_OutputImage, g_OutputImageSlot, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "Output");
}

// Where the shaders find the resources they all share, by name.
static void CreateGlobalResources()
{
    g_GlobalResources.AddPushData("Frame", offsetof(PushData, frame), sizeof(shared::FrameConstants));

    // Use vkCmdPushDataEXT to bind the TLAS to shader. Buffers and TLASes can write their addresses into push data for shader binding.
    g_GlobalResources.AddPushAddress("AccelStruct", offsetof(PushData, accelStruct));

    // Write the accumulation and output image descriptors into the descriptor heap and use their byte offsets to specify where the descriptors are in the descriptor heap.
    // Images can't be bound using push data.
    g_GlobalResources.AddHeapResource("Accumulation", g_ResourceHeap.SlotOffset(g_AccumulationImageSlot));
    g_GlobalResources.AddHeapResource("Output", g_ResourceHeap.SlotOffset(g_OutputImageSlot));

    // A texture a shader reads from its own record is one of these slots, held in the record as the slot's index:
    // that turns into the offset of its descriptor with the size of a slot.
    g_GlobalResources.SetHeapSlotSize(g_ResourceHeap.SlotSize());

    // One sampler for every texture of every material: the textures differ per material, how they are filtered does
    // not. It lives in the sampler heap, which is bound alongside the resource one.
    g_GlobalResources.AddHeapResource("LinearSampler", g_SamplerHeap.SlotOffset(g_LinearSamplerSlot));
}

// Trilinear and repeating. There is one mip level to sample for now, so the mip filter only says what would happen
// once there are more.
static bool CreateLinearSampler()
{
    g_LinearSamplerSlot = g_SamplerHeap.Allocate();

    if (g_LinearSamplerSlot == UINT32_MAX)
        return false;

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    return g_SamplerHeap.WriteSampler(g_LinearSamplerSlot, samplerInfo);
}

// Brings the pipeline and its shader binding table up to date with the scene. They are kept as long as they fit it.
static bool UpdateRayTracingPipeline()
{
    GetHitRecords(g_HitRecords);

    return g_RayTracingPipeline.Update(g_RayGenShader, g_HitRecords);
}

bool InitializeRenderer(HINSTANCE hInstance, HWND hWnd)
{
    g_Window = hWnd;
    GetWindowText(hWnd, g_WindowTitle, sizeof(g_WindowTitle));

    if (!InitializeVulkan(hInstance, hWnd))
        return false;

    if (!g_ResourceHeap.Create(DescriptorHeap::Type::Resource, 1024, "Resource heap"))
    {
        MessageBox(hWnd, "Couldn't create the resource descriptor heap. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    g_AccumulationImageSlot = g_ResourceHeap.Allocate();
    g_OutputImageSlot = g_ResourceHeap.Allocate();

    if (!g_SamplerHeap.Create(DescriptorHeap::Type::Sampler, 16, "Sampler heap") || !CreateLinearSampler())
    {
        MessageBox(hWnd, "Couldn't create the sampler descriptor heap. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    if (!UpdateRenderTargets())
    {
        MessageBox(hWnd, "Couldn't create the render target images. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    if (sizeof(PushData) > g_VkPhysicalDevices[g_CurrentPhysicalDevice].deviceDescriptorHeapProperties.maxPushDataSize)
    {
        MessageBox(hWnd, "The push data is larger than this GPU supports. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    CreateGlobalResources();

    const char* missEntryPoints[shared::RAY_TYPE_COUNT];
    missEntryPoints[shared::RAY_TYPE_RADIANCE] = "MissRadiance";
    missEntryPoints[shared::RAY_TYPE_SHADOW] = "MissShadow";

    if (!g_RayGenShader.Create("RayGen", "RayGen", missEntryPoints, g_GlobalResources) || !CreateRayTracingScene(g_GlobalResources, g_ResourceHeap))
    {
        MessageBox(hWnd, "Couldn't load the ray tracing shaders from the shaders folder. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    if (!UpdateRayTracingPipeline())
    {
        MessageBox(hWnd, "Couldn't create the ray tracing pipeline and its shader binding table. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    g_TraceTimer.Create(g_MaxFramesInFlight, "Trace timer");

    g_RendererReady = true;

    return true;
}

void OnWindowResized(uint32_t width, uint32_t height)
{
    if (g_RendererReady && (width != g_VkSwapChainExtent.width || height != g_VkSwapChainExtent.height))
        g_WindowResized = true;
}

void OnMouseDrag(float deltaX, float deltaY)
{
    g_Camera.yaw -= deltaX * 0.006f;
    g_Camera.pitch = std::clamp(g_Camera.pitch + deltaY * 0.006f, Radians(2.0f), Radians(85.0f));
    g_AccumulatedFrames = 0;
}

void OnMouseWheel(float steps)
{
    g_Camera.distance = std::clamp(g_Camera.distance * std::pow(0.9f, steps), 3.0f, 40.0f);
    g_AccumulatedFrames = 0;
}

void ShutdownRenderer()
{
    g_RendererReady = false;

    if (g_VkDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(g_VkDevice);
    }

    g_TraceTimer.Destroy();
    g_RayTracingPipeline.Destroy();
    g_RayGenShader.Destroy();
    DestroyImage(g_AccumulationImage);
    DestroyImage(g_OutputImage);
    g_ResourceHeap.Destroy();
    g_SamplerHeap.Destroy();

    DestroyRayTracingScene();

    ShutdownVulkan();
}

static VkCommandBuffer BeginFrame(uint32_t& imageIndex)
{
    if (g_VkSwapChain == VK_NULL_HANDLE && !RecreateSwapChainAndResetPresentId())
        return VK_NULL_HANDLE;

    if (g_VkPresentWaitEnabled && g_PresentId > 1)
        vkWaitForPresentKHR(g_VkDevice, g_VkSwapChain, g_PresentId - 1, 100000000);

    vkWaitForFences(g_VkDevice, 1, &g_VkFrameFence[g_CurrentFrame], VK_TRUE, (uint64_t)-1);

    double traceMilliseconds = 0.0;

    if (g_TraceTimer.Read((uint32_t)g_CurrentFrame, traceMilliseconds))
        ReportTraceTime(traceMilliseconds);

    if (g_WindowResized)
    {
        if (!RecreateSwapChainAndResetPresentId())
            return VK_NULL_HANDLE;

        g_WindowResized = false;
    }

    if (!UpdateRenderTargets() || !UpdateRayTracingPipeline())
        return VK_NULL_HANDLE;

    VkResult result = vkAcquireNextImageKHR(g_VkDevice, g_VkSwapChain, (uint64_t)-1, g_VkImageAvailableSemaphore[g_CurrentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapChainAndResetPresentId();
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
    g_ResourceHeap.Bind(cmdBuffer);
    g_SamplerHeap.Bind(cmdBuffer);

    BuildTLAS(cmdBuffer, g_CurrentFrame);

    // The accumulation image stays in GENERAL: this barrier orders the previous frame's writes before this frame's reads.
    const VkImageLayout accumulationLayout = g_AccumulatedFrames == 0 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;

    TransitionImage(cmdBuffer, g_AccumulationImage.image, accumulationLayout, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    TransitionImage(cmdBuffer, g_OutputImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    g_RayTracingPipeline.Bind(cmdBuffer);

    PushData pushData = { g_Camera.MakeFrameConstants(g_OutputImage.extent.width, g_OutputImage.extent.height), g_Tlas.address };
    pushData.frame.frameIndex = g_FrameIndex;
    pushData.frame.accumulatedFrames = g_AccumulatedFrames;
    pushData.frame.jitter = { Halton(g_AccumulatedFrames + 1, 2), Halton(g_AccumulatedFrames + 1, 3) };

    VkPushDataInfoEXT pushInfo = {};
    pushInfo.sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT;
    pushInfo.offset = 0;
    pushInfo.data.address = &pushData;
    pushInfo.data.size = sizeof(pushData);

    vkCmdPushDataEXT(cmdBuffer, &pushInfo);

    g_TraceTimer.Begin(cmdBuffer, (uint32_t)g_CurrentFrame);

    g_RayTracingPipeline.TraceRays(cmdBuffer, g_OutputImage.extent.width, g_OutputImage.extent.height);

    g_TraceTimer.End(cmdBuffer, (uint32_t)g_CurrentFrame);

    g_AccumulatedFrames++;

    TransitionImage(cmdBuffer, g_OutputImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
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

    g_PresentId++;

    VkPresentIdKHR presentIdInfo = {};
    presentIdInfo.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
    presentIdInfo.swapchainCount = 1;
    presentIdInfo.pPresentIds = &g_PresentId;

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = g_VkPresentWaitEnabled ? &presentIdInfo : nullptr;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &g_VkRenderingFinishedSemaphore[imageIndex];
    presentInfo.pSwapchains = &g_VkSwapChain;
    presentInfo.swapchainCount = 1;
    presentInfo.pResults = nullptr;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(g_VkQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        RecreateSwapChainAndResetPresentId();

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
