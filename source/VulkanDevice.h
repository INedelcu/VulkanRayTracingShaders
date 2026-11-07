#pragma once

#include "Application.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

// The headers come from the Vulkan SDK that VULKAN_SDK points to; descriptor heaps need SDK 1.4.363 or newer.
#ifndef VK_EXT_descriptor_heap
#error "The Vulkan SDK that VULKAN_SDK points to is too old: its headers lack VK_EXT_descriptor_heap. Download the Windows installer of Vulkan SDK 1.4.363 or newer from https://vulkan.lunarg.com/sdk/home, run it, then restart Visual Studio so it sees the new VULKAN_SDK."
#endif

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
    VkPhysicalDeviceDescriptorHeapPropertiesEXT         deviceDescriptorHeapProperties;
    VkPhysicalDeviceVulkan12Features                    deviceVulkan12Features;
    VkPhysicalDeviceVulkan13Features                    deviceVulkan13Features;
    VkPhysicalDeviceVulkan14Features                    deviceVulkan14Features;
    VkPhysicalDeviceDescriptorHeapFeaturesEXT           deviceDescriptorHeapFeatures;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR       deviceRayTracingPipelineFeatures;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR    deviceAccelerationStructureFeatures;
    VkPhysicalDevicePresentIdFeaturesKHR                devicePresentIdFeatures;
    VkPhysicalDevicePresentWaitFeaturesKHR              devicePresentWaitFeatures;
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
extern uint32_t                             g_VkQueueFamilyIndex;
extern VkQueue                              g_VkQueue;
extern std::vector<VkSemaphore>             g_VkImageAvailableSemaphore;
extern std::vector<VkSemaphore>             g_VkRenderingFinishedSemaphore;
extern std::vector<VkFence>                 g_VkFrameFence;
extern VkCommandPool                        g_VkCommandPool;
extern std::vector<VkCommandBuffer>         g_VkCommandBuffers;
extern bool                                 g_VkPresentWaitEnabled;

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
