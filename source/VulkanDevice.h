#pragma once

#include "Application.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

#include "VulkanFunctions.h"

#include <vector>

struct PhysicalGfxDeviceInfo
{
    VkPhysicalDevice                                    device;
    VkPhysicalDeviceProperties2                         deviceProperties;
    VkPhysicalDeviceFeatures2                           deviceFeatures;
    VkPhysicalDeviceMemoryProperties                    deviceMemoryProperties;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR     deviceRayTracingPipelineProperties;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR  deviceAccelerationStructureProperties;
    VkPhysicalDeviceVulkan12Features                    deviceVulkan12Features;
    VkPhysicalDeviceVulkan13Features                    deviceVulkan13Features;
    VkPhysicalDeviceVulkan14Features                    deviceVulkan14Features;
    VkPhysicalDeviceDescriptorHeapFeaturesEXT           deviceDescriptorHeapFeatures;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR       deviceRayTracingPipelineFeatures;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR    deviceAccelerationStructureFeatures;
    std::vector<VkQueueFamilyProperties>                deviceQueueFamilyProperties;
#ifdef VK_KHR_surface
    VkSurfaceCapabilitiesKHR                            deviceSurfaceCaps;
    std::vector<VkSurfaceFormatKHR>                     deviceSurfaceFormats;
    std::vector<VkPresentModeKHR>                       deviceSurfacePresentModes;
#endif
};

extern uint32_t                             g_MaxFramesInFlight;
extern std::vector<PhysicalGfxDeviceInfo>   g_VkPhysicalDevices;
extern size_t                               g_CurrentPhysicalDevice;
extern VkDevice                             g_VkDevice;
extern VkQueue                              g_VkQueue;
extern std::vector<VkSemaphore>             g_VkImageAvailableSemaphore;
extern std::vector<VkSemaphore>             g_VkRenderingFinishedSemaphore;
extern std::vector<VkFence>                 g_VkFrameFence;
extern VkCommandPool                        g_VkCommandPool;
extern std::vector<VkCommandBuffer>         g_VkCommandBuffers;

#ifdef VK_KHR_swapchain
extern VkSwapchainKHR                       g_VkSwapChain;
extern std::vector<VkImage>                 g_VkSwapChainImages;
extern VkExtent2D                           g_VkSwapChainExtent;
#endif

bool InitializeVulkan(HINSTANCE hInstance, HWND hWnd);
void ShutdownVulkan();
bool RecreateSwapChain();
VkCommandBuffer BeginOneTimeCommands();
void EndOneTimeCommands(VkCommandBuffer commandBuffer);
void SetVkObjectName(uint64_t handle, VkObjectType type, const char* name);
