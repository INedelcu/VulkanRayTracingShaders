#include "Renderer.h"

#include "Camera.h"
#include "DescriptorHeap.h"
#include "GpuTimer.h"
#include "RayTracingPipeline.h"
#include "RayTracingScene.h"
#include "ShaderReflection.h"
#include "Shaders.h"
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
Image              g_AccumulationImage;
Image              g_OutputImage;
uint32_t           g_AccumulationImageSlot = UINT32_MAX;
uint32_t           g_OutputImageSlot = UINT32_MAX;
RayTracingPipeline g_RayTracingPipeline;
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

bool InitializeRenderer(HINSTANCE hInstance, HWND hWnd)
{
    g_Window = hWnd;
    GetWindowText(hWnd, g_WindowTitle, sizeof(g_WindowTitle));

    if (!InitializeVulkan(hInstance, hWnd))
        return false;

#ifdef VK_KHR_acceleration_structure
    CreateRayTracingScene();
#endif

    if (!g_ResourceHeap.Create(DescriptorHeap::Type::Resource, 1024, "Resource heap"))
    {
        MessageBox(hWnd, "Couldn't create the resource descriptor heap. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    g_AccumulationImageSlot = g_ResourceHeap.Allocate();
    g_OutputImageSlot = g_ResourceHeap.Allocate();

    if (!UpdateRenderTargets())
    {
        MessageBox(hWnd, "Couldn't create the render target images. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    const std::vector<uint32_t> rayGenSpirv = LoadSpirv("RayGen");
    VkShaderModule rayGenShader = VK_NULL_HANDLE;

    if (!rayGenSpirv.empty())
        rayGenShader = CreateShaderModule(rayGenSpirv, "RayGen");

    if (rayGenShader == VK_NULL_HANDLE)
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

    if (sizeof(PushData) > g_VkPhysicalDevices[g_CurrentPhysicalDevice].deviceDescriptorHeapProperties.maxPushDataSize)
    {
        vkDestroyShaderModule(g_VkDevice, rayGenShader, nullptr);
        MessageBox(hWnd, "The push data is larger than this GPU supports. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    std::vector<VkDescriptorSetAndBindingMappingEXT> mappings;

    if (const ShaderResourceBinding* frame = rayGenReflection.Find("Frame"))
        mappings.push_back(PushDataMapping(frame->set, frame->binding, offsetof(PushData, frame)));

    // Use vkCmdPushDataEXT to bind the TLAS to shader. Buffers and TLASes can write their addresses into push data for shader binding.
    if (const ShaderResourceBinding* accelStruct = rayGenReflection.Find("AccelStruct"))
        mappings.push_back(PushAddressMapping(accelStruct->set, accelStruct->binding, VK_SPIRV_RESOURCE_TYPE_ACCELERATION_STRUCTURE_BIT_EXT, offsetof(PushData, accelStruct)));

    // Write the accumulation and output image descriptors into the descriptor heap and use their byte offsets to specify where the descriptors are in the descriptor heap.
    // Images can't be bound using push data.
    if (const ShaderResourceBinding* accumulation = rayGenReflection.Find("Accumulation"))
        mappings.push_back(HeapConstantOffsetMapping(accumulation->set, accumulation->binding, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, g_ResourceHeap.SlotOffset(g_AccumulationImageSlot)));

    if (const ShaderResourceBinding* output = rayGenReflection.Find("Output"))
        mappings.push_back(HeapConstantOffsetMapping(output->set, output->binding, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, g_ResourceHeap.SlotOffset(g_OutputImageSlot)));

    // Also use VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT because of https://github.com/microsoft/DirectXShaderCompiler/issues/8492
    const VkSpirvResourceTypeFlagsEXT storageBufferMask = VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT | VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT;

    if (const ShaderResourceBinding* vertices = rayGenReflection.Find("Vertices"))
        mappings.push_back(ShaderRecordAddressMapping(vertices->set, vertices->binding, storageBufferMask, offsetof(MeshRecord, vertices)));

    if (const ShaderResourceBinding* indices = rayGenReflection.Find("Indices"))
        mappings.push_back(ShaderRecordAddressMapping(indices->set, indices->binding, storageBufferMask, offsetof(MeshRecord, indices)));

    const bool pipelineCreated = CreateRayTracingPipeline(g_RayTracingPipeline, rayGenShader, mappings);

    vkDestroyShaderModule(g_VkDevice, rayGenShader, nullptr);

    if (!pipelineCreated)
    {
        MessageBox(hWnd, "Couldn't create the ray tracing pipeline. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    std::vector<MeshRecord> hitRecords;

    for (const Instance& instance : g_Instances)
        hitRecords.push_back({ g_Meshes[instance.mesh].vertexBuffer.address, g_Meshes[instance.mesh].indexBuffer.address });

    if (!CreateShaderBindingTable(g_RayTracingPipeline, hitRecords))
    {
        MessageBox(hWnd, "Couldn't create the shader binding table. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
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
    DestroyRayTracingPipeline(g_RayTracingPipeline);
    DestroyImage(g_AccumulationImage);
    DestroyImage(g_OutputImage);
    g_ResourceHeap.Destroy();

#ifdef VK_KHR_acceleration_structure
    DestroyRayTracingScene();
#endif

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

    if (!UpdateRenderTargets())
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

#ifdef VK_KHR_acceleration_structure
    BuildTLAS(cmdBuffer, g_CurrentFrame);
#endif

    // The accumulation image stays in GENERAL: this barrier orders the previous frame's writes before this frame's reads.
    const VkImageLayout accumulationLayout = g_AccumulatedFrames == 0 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;

    TransitionImage(cmdBuffer, g_AccumulationImage.image, accumulationLayout, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    TransitionImage(cmdBuffer, g_OutputImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_RayTracingPipeline.pipeline);

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

    vkCmdTraceRaysKHR(cmdBuffer, &g_RayTracingPipeline.raygenRegion, &g_RayTracingPipeline.missRegion, &g_RayTracingPipeline.hitRegion, &g_RayTracingPipeline.callableRegion,
                      g_OutputImage.extent.width, g_OutputImage.extent.height, 1);

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
