#pragma once

#define VULKAN_EXTERN_FUNCTION_POINTER(f) extern PFN_##f f;

#define VULKAN_GLOBAL_FUNCTION_LIST(macro) \
    macro(vkCreateInstance) \
    macro(vkEnumerateInstanceExtensionProperties) \
    macro(vkEnumerateInstanceLayerProperties) \
    macro(vkEnumerateInstanceVersion) \

#ifdef VK_KHR_win32_surface
#define VULKAN_MACRO_EXT_VK_KHR_win32_surface(macro) \
    macro(vkCreateWin32SurfaceKHR) \
    macro(vkGetPhysicalDeviceWin32PresentationSupportKHR)
#else
#define VULKAN_MACRO_EXT_VK_KHR_win32_surface(macro)
#endif

#ifdef VK_KHR_surface
#define VULKAN_MACRO_EXT_VK_KHR_surface(macro) \
    macro(vkDestroySurfaceKHR) \
    macro(vkGetPhysicalDeviceSurfaceSupportKHR) \
    macro(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    macro(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    macro(vkGetPhysicalDeviceSurfacePresentModesKHR)
#else
#define VULKAN_MACRO_EXT_VK_KHR_surface(macro)
#endif

#ifdef VK_EXT_debug_utils
#define VULKAN_MACRO_EXT_VK_EXT_debug_utils(macro) \
    macro(vkCreateDebugUtilsMessengerEXT) \
    macro(vkDestroyDebugUtilsMessengerEXT) \
    macro(vkSetDebugUtilsObjectNameEXT)
#else
#define VULKAN_MACRO_EXT_VK_EXT_debug_utils(macro)
#endif

#ifdef VK_KHR_swapchain
#define VULKAN_MACRO_EXT_VK_KHR_swapchain(macro) \
    macro(vkCreateSwapchainKHR) \
    macro(vkDestroySwapchainKHR) \
    macro(vkGetSwapchainImagesKHR) \
    macro(vkAcquireNextImageKHR) \
    macro(vkQueuePresentKHR)
#else
#define VULKAN_MACRO_EXT_VK_KHR_swapchain(macro)
#endif

#define VULKAN_EXTENSION_LIST(ext, macro) VULKAN_MACRO_EXT_ ## ext (macro)

#ifdef VK_KHR_acceleration_structure
#define VULKAN_MACRO_EXT_VK_KHR_acceleration_structure(macro) \
    macro(vkGetAccelerationStructureBuildSizesKHR) \
    macro(vkGetAccelerationStructureDeviceAddressKHR) \
    macro(vkCreateAccelerationStructureKHR) \
    macro(vkDestroyAccelerationStructureKHR) \
    macro(vkCmdBuildAccelerationStructuresKHR)
#else
#define VULKAN_MACRO_EXT_VK_KHR_acceleration_structure(macro)
#endif

#ifdef VK_KHR_ray_tracing_pipeline
#define VULKAN_MACRO_EXT_VK_KHR_ray_tracing_pipeline(macro) \
    macro(vkCreateRayTracingPipelinesKHR) \
    macro(vkGetRayTracingShaderGroupHandlesKHR) \
    macro(vkCmdTraceRaysKHR)
#else
#define VULKAN_MACRO_EXT_VK_KHR_ray_tracing_pipeline(macro)
#endif

#ifdef VK_KHR_present_wait
#define VULKAN_MACRO_EXT_VK_KHR_present_wait(macro) \
    macro(vkWaitForPresentKHR)
#else
#define VULKAN_MACRO_EXT_VK_KHR_present_wait(macro)
#endif

#ifdef VK_EXT_descriptor_heap
#define VULKAN_MACRO_EXT_VK_EXT_descriptor_heap(macro) \
    macro(vkWriteResourceDescriptorsEXT) \
    macro(vkWriteSamplerDescriptorsEXT) \
    macro(vkCmdBindResourceHeapEXT) \
    macro(vkCmdBindSamplerHeapEXT) \
    macro(vkCmdPushDataEXT)
#define VULKAN_MACRO_EXT_VK_EXT_descriptor_heap_physical_device(macro) \
    macro(vkGetPhysicalDeviceDescriptorSizeEXT)
#else
#define VULKAN_MACRO_EXT_VK_EXT_descriptor_heap(macro)
#define VULKAN_MACRO_EXT_VK_EXT_descriptor_heap_physical_device(macro)
#endif

#define VULKAN_INSTANCE_FUNCTION_LIST(macro) \
    macro(vkDestroyInstance) \
    macro(vkEnumeratePhysicalDevices) \
    macro(vkGetPhysicalDeviceProperties) \
    macro(vkGetPhysicalDeviceProperties2) \
    macro(vkGetPhysicalDeviceFeatures) \
    macro(vkGetPhysicalDeviceFeatures2) \
    macro(vkGetPhysicalDeviceMemoryProperties) \
    macro(vkGetPhysicalDeviceQueueFamilyProperties) \
    macro(vkCreateDevice) \
    macro(vkGetDeviceProcAddr) \
    macro(vkEnumerateDeviceExtensionProperties) \
    VULKAN_EXTENSION_LIST(VK_KHR_win32_surface, macro)  \
    VULKAN_EXTENSION_LIST(VK_KHR_surface, macro) \
    VULKAN_EXTENSION_LIST(VK_EXT_debug_utils, macro) \
    VULKAN_EXTENSION_LIST(VK_EXT_descriptor_heap_physical_device, macro)

#define VULKAN_DEVICE_FUNCTION_LIST(macro) \
    macro(vkGetDeviceQueue) \
    macro(vkGetBufferDeviceAddress) \
    macro(vkDestroyDevice) \
    macro(vkDeviceWaitIdle) \
    macro(vkQueueWaitIdle) \
    macro(vkCreateSemaphore) \
    macro(vkDestroySemaphore) \
    macro(vkCreateFence) \
    macro(vkDestroyFence) \
    macro(vkCreateImageView) \
    macro(vkDestroyImageView) \
    macro(vkCreateCommandPool) \
    macro(vkDestroyCommandPool) \
    macro(vkAllocateCommandBuffers) \
    macro(vkFreeCommandBuffers) \
    macro(vkCmdPipelineBarrier) \
    macro(vkCmdPipelineBarrier2) \
    macro(vkBeginCommandBuffer) \
    macro(vkEndCommandBuffer) \
    macro(vkQueueSubmit) \
    macro(vkWaitForFences) \
    macro(vkResetFences) \
    macro(vkCmdClearColorImage) \
    macro(vkCmdBlitImage) \
    macro(vkCreateImage) \
    macro(vkDestroyImage) \
    macro(vkGetImageMemoryRequirements) \
    macro(vkBindImageMemory) \
    macro(vkAllocateMemory) \
    macro(vkCreateBuffer) \
    macro(vkDestroyBuffer) \
    macro(vkCmdCopyBuffer) \
    macro(vkBindBufferMemory) \
    macro(vkMapMemory) \
    macro(vkUnmapMemory) \
    macro(vkFreeMemory) \
    macro(vkGetBufferMemoryRequirements) \
    macro(vkCreateShaderModule) \
    macro(vkDestroyShaderModule) \
    macro(vkCmdBindPipeline) \
    macro(vkDestroyPipeline) \
    macro(vkCreateQueryPool) \
    macro(vkDestroyQueryPool) \
    macro(vkCmdResetQueryPool) \
    macro(vkCmdWriteTimestamp2) \
    macro(vkGetQueryPoolResults) \
    VULKAN_EXTENSION_LIST(VK_KHR_acceleration_structure, macro) \
    VULKAN_EXTENSION_LIST(VK_KHR_ray_tracing_pipeline, macro) \
    VULKAN_EXTENSION_LIST(VK_EXT_descriptor_heap, macro) \
    VULKAN_EXTENSION_LIST(VK_KHR_present_wait, macro) \
    VULKAN_EXTENSION_LIST(VK_KHR_swapchain, macro)

VULKAN_GLOBAL_FUNCTION_LIST(VULKAN_EXTERN_FUNCTION_POINTER)
VULKAN_INSTANCE_FUNCTION_LIST(VULKAN_EXTERN_FUNCTION_POINTER)
VULKAN_DEVICE_FUNCTION_LIST(VULKAN_EXTERN_FUNCTION_POINTER)
