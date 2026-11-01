#include "VulkanDevice.h"

#include <algorithm>
#include <limits>

uint32_t g_MaxFramesInFlight = 3;

std::vector<PhysicalGfxDeviceInfo>      g_VkPhysicalDevices;
std::vector<VkExtensionProperties>      g_VkInstanceExtensions;
std::vector<VkExtensionProperties>      g_VkDeviceExtensions;
std::vector<VkLayerProperties>          g_VkLayerProperties;
size_t                                  g_CurrentPhysicalDevice = 0;


PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

HMODULE                     g_VulkanLib = NULL;
VkInstance                  g_VkInstance = VK_NULL_HANDLE;
VkDevice                    g_VkDevice = VK_NULL_HANDLE;
VkQueue                     g_VkQueue = VK_NULL_HANDLE;
std::vector<VkSemaphore>    g_VkImageAvailableSemaphore;
std::vector<VkSemaphore>    g_VkRenderingFinishedSemaphore;
std::vector<VkFence>        g_VkFrameFence;
VkCommandPool               g_VkCommandPool = VK_NULL_HANDLE;
std::vector<VkCommandBuffer> g_VkCommandBuffers;
uint32_t                    g_VkQueueFamilyIndex;

#ifdef VK_KHR_surface
VkSurfaceKHR                g_VkPresentationSurface = VK_NULL_HANDLE;
#endif

#ifdef VK_KHR_swapchain
VkSwapchainKHR              g_VkSwapChain = VK_NULL_HANDLE;
std::vector<VkImage>        g_VkSwapChainImages;
#endif

#ifdef VK_EXT_debug_utils
VkDebugUtilsMessengerEXT    g_VkDebugUtilsMessenger = VK_NULL_HANDLE;
bool                        g_VkDebugUtilsEnabled = false;
#endif

template<typename FunctionPtr>
bool LoadInstanceFunction(FunctionPtr& function, const char* functionName, VkInstance vkInstance)
{
    function = (FunctionPtr)vkGetInstanceProcAddr(vkInstance, functionName);
    return function != NULL;
}

template<typename FunctionPtr>
bool LoadDeviceFunction(FunctionPtr& function, const char* functionName, VkDevice vkDevice)
{
    function = (FunctionPtr)vkGetDeviceProcAddr(vkDevice, functionName);
    return function != NULL;
}

#define VULKAN_DECLARE_FUNCTION(f) PFN_##f f = NULL;

VULKAN_GLOBAL_FUNCTION_LIST(VULKAN_DECLARE_FUNCTION)
VULKAN_INSTANCE_FUNCTION_LIST(VULKAN_DECLARE_FUNCTION)
VULKAN_DEVICE_FUNCTION_LIST(VULKAN_DECLARE_FUNCTION)

#define VULKAN_LOAD_NULL_INSTANCE_FUNCTION(f) LoadInstanceFunction(f, #f, NULL);

static VKAPI_ATTR VkBool32 VKAPI_CALL ValidationLayerReportCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    char message[2048 * 4];
    _snprintf_s(message, _TRUNCATE, "Vulkan Validation Layer - %s\n", pCallbackData->pMessage);
    OutputDebugString(message);

    return VK_FALSE;
}

#if defined(VK_EXT_debug_utils) && defined(_DEBUG)
void SetVkObjectName(uint64_t handle, VkObjectType type, const char* name)
{
    if (!g_VkDebugUtilsEnabled)
        return;

    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectHandle = handle;
    info.objectType = type;
    info.pObjectName = name;

    vkSetDebugUtilsObjectNameEXT(g_VkDevice, &info);
}
#else
void SetVkObjectName(uint64_t handle, VkObjectType type, const char* name)
{
}
#endif

const std::vector<const char*> kMandatoryInstanceExtensions =
{
    VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
};

bool IsExtensionAvailable(const std::vector<VkExtensionProperties>& extensionsList, const char* extensionName)
{
    for (size_t i = 0; i < extensionsList.size(); ++i)
    {
        if (strcmp(extensionsList[i].extensionName, extensionName) == 0)
            return true;
    }
    return false;
}

bool IsLayerAvailable(const std::vector<VkLayerProperties>& layerList, const char* layerName)
{
    for (size_t i = 0; i < layerList.size(); ++i)
    {
        if (strcmp(layerList[i].layerName, layerName) == 0)
            return true;
    }
    return false;
}

std::vector<const char*> kMandatoryDeviceExtensions =
{
#ifdef VK_KHR_swapchain
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#endif

#ifdef VK_KHR_ray_tracing_pipeline
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
#endif

#ifdef VK_KHR_acceleration_structure
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
#endif

#ifdef VK_EXT_descriptor_heap
	VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
#endif

#ifdef VK_KHR_deferred_host_operations
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
#endif
};

bool CreateSwapChain()
{
    VkSurfaceCapabilitiesKHR surfProperties;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_VkPhysicalDevices[g_CurrentPhysicalDevice].device, g_VkPresentationSurface, &surfProperties);

    if (surfProperties.currentExtent.width == 0 || surfProperties.currentExtent.height == 0)
        return false;

    VkSwapchainKHR oldSwapChain = g_VkSwapChain;

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = g_VkPresentationSurface;
    createInfo.flags = 0;
    createInfo.minImageCount = g_MaxFramesInFlight;
    createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = surfProperties.currentExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = NULL;
    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapChain;

    VkResult result = vkCreateSwapchainKHR(g_VkDevice, &createInfo, NULL, &g_VkSwapChain);

    if (oldSwapChain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(g_VkDevice, oldSwapChain, NULL);

    for (size_t i = 0; i < g_VkRenderingFinishedSemaphore.size(); i++)
        vkDestroySemaphore(g_VkDevice, g_VkRenderingFinishedSemaphore[i], nullptr);

    g_VkRenderingFinishedSemaphore.clear();
    g_VkSwapChainImages.clear();

    if (result != VK_SUCCESS)
    {
        g_VkSwapChain = VK_NULL_HANDLE;
        return false;
    }

    unsigned int imageCount;
    vkGetSwapchainImagesKHR(g_VkDevice, g_VkSwapChain, &imageCount, nullptr);
    g_VkSwapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(g_VkDevice, g_VkSwapChain, &imageCount, g_VkSwapChainImages.data());

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    g_VkRenderingFinishedSemaphore.resize(g_VkSwapChainImages.size());

    for (size_t i = 0; i < g_VkRenderingFinishedSemaphore.size(); i++)
    {
        if (vkCreateSemaphore(g_VkDevice, &semaphoreInfo, nullptr, &g_VkRenderingFinishedSemaphore[i]) != VK_SUCCESS)
            return false;
    }

    return true;
}

bool RecreateSwapChain()
{
    vkDeviceWaitIdle(g_VkDevice);

    return CreateSwapChain();
}

bool InitializeVulkan(HINSTANCE hInstance, HWND hWnd)
{
    g_VulkanLib = LoadLibrary("vulkan-1.dll");
    if (g_VulkanLib == nullptr)
    {
        MessageBox(hWnd, "Vulkan drivers not installed or vulkan-1.dll not found. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(g_VulkanLib, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr)
    {
        MessageBox(hWnd, "vkGetInstanceProcAddr not found. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    VULKAN_LOAD_NULL_INSTANCE_FUNCTION(vkCreateInstance);
    VULKAN_LOAD_NULL_INSTANCE_FUNCTION(vkEnumerateInstanceExtensionProperties);
    VULKAN_LOAD_NULL_INSTANCE_FUNCTION(vkEnumerateInstanceLayerProperties);
    VULKAN_LOAD_NULL_INSTANCE_FUNCTION(vkEnumerateInstanceVersion);

    // Enumerate general extensions
    uint32_t extensionsCount = 0;
    if ((vkEnumerateInstanceExtensionProperties(nullptr, &extensionsCount, nullptr) != VK_SUCCESS) || (extensionsCount == 0))
        return false;

    g_VkInstanceExtensions.resize(extensionsCount);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionsCount, g_VkInstanceExtensions.data()) != VK_SUCCESS)
        return false;

    for (size_t i = 0; i < kMandatoryInstanceExtensions.size(); i++)
    {
        if (IsExtensionAvailable(g_VkInstanceExtensions, kMandatoryInstanceExtensions[i]) == false)
            return false;
    }

    std::vector<const char*> enabledExtensions;
    std::vector<const char*> enabledLayers;

    uint32_t layersCount = 0;
    vkEnumerateInstanceLayerProperties(&layersCount, nullptr);
    g_VkLayerProperties.resize(layersCount);
    vkEnumerateInstanceLayerProperties(&layersCount, g_VkLayerProperties.data());

    uint32_t version;
    vkEnumerateInstanceVersion(&version);

#if _DEBUG
#if VK_EXT_debug_utils
    if (IsExtensionAvailable(g_VkInstanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        g_VkDebugUtilsEnabled = true;
    }
#endif

    if (IsLayerAvailable(g_VkLayerProperties, "VK_LAYER_KHRONOS_validation"))
        enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    enabledExtensions.insert(enabledExtensions.end(), kMandatoryInstanceExtensions.begin(), kMandatoryInstanceExtensions.end());

    VkApplicationInfo applicationInfo =
    {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "VulkanRayTracing",
        VK_MAKE_VERSION(1, 0, 0),
        "VulkanRayTracing",
        VK_MAKE_VERSION(1, 0, 0),
		VK_API_VERSION_1_4
    };

    VkInstanceCreateInfo instanceCreateInfo =
    {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &applicationInfo,
        (uint32_t)enabledLayers.size(),
        enabledLayers.data(),
        (uint32_t)enabledExtensions.size(),
        enabledExtensions.data()
    };

    if (vkCreateInstance(&instanceCreateInfo, nullptr, &g_VkInstance) != VK_SUCCESS)
        return false;

#define VULKAN_LOAD_INSTANCE_FUNCTION(f) LoadInstanceFunction(f, #f, g_VkInstance);

    VULKAN_INSTANCE_FUNCTION_LIST(VULKAN_LOAD_INSTANCE_FUNCTION);

#if _DEBUG
    // Initialize "VK_EXT_debug_utils"
#if VK_EXT_debug_utils
    VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo = {};
    debugUtilsMessengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugUtilsMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    debugUtilsMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugUtilsMessengerCreateInfo.pfnUserCallback = ValidationLayerReportCallback;
    debugUtilsMessengerCreateInfo.pUserData = nullptr; // Optional

    if (g_VkDebugUtilsEnabled)
        vkCreateDebugUtilsMessengerEXT(g_VkInstance, &debugUtilsMessengerCreateInfo, nullptr, &g_VkDebugUtilsMessenger);
#endif
#endif

    // Create a presentation surface for the main window
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo =
    {
        VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        nullptr,
        0,
        hInstance,
        hWnd
    };

    if (vkCreateWin32SurfaceKHR(g_VkInstance, &surfaceCreateInfo, nullptr, &g_VkPresentationSurface) != VK_SUCCESS)
    {
        MessageBox(hWnd, "Couldn't create Vulkan presentation surface. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    // Enumerate physical devices and retrieve associated properties
    uint32_t physicalDeviceCount = 0;
    vkEnumeratePhysicalDevices(g_VkInstance, &physicalDeviceCount, nullptr);

    g_VkPhysicalDevices.resize(physicalDeviceCount);

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    vkEnumeratePhysicalDevices(g_VkInstance, &physicalDeviceCount, physicalDevices.data());

    for (size_t i = 0; i < physicalDevices.size(); i++)
    {
        g_VkPhysicalDevices[i].device = physicalDevices[i];

        g_VkPhysicalDevices[i].deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        g_VkPhysicalDevices[i].deviceRayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

        g_VkPhysicalDevices[i].deviceProperties.pNext = (void*)&g_VkPhysicalDevices[i].deviceRayTracingPipelineProperties;

        g_VkPhysicalDevices[i].deviceAccelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        g_VkPhysicalDevices[i].deviceRayTracingPipelineProperties.pNext = (void*)&g_VkPhysicalDevices[i].deviceAccelerationStructureProperties;

        g_VkPhysicalDevices[i].deviceRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

        g_VkPhysicalDevices[i].deviceAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        g_VkPhysicalDevices[i].deviceRayTracingPipelineFeatures.pNext = (void*)&g_VkPhysicalDevices[i].deviceAccelerationStructureFeatures;

        g_VkPhysicalDevices[i].deviceDescriptorHeapFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
        g_VkPhysicalDevices[i].deviceDescriptorHeapFeatures.pNext = (void*)&g_VkPhysicalDevices[i].deviceRayTracingPipelineFeatures;

        g_VkPhysicalDevices[i].deviceVulkan14Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
        g_VkPhysicalDevices[i].deviceVulkan14Features.pNext = (void*)&g_VkPhysicalDevices[i].deviceDescriptorHeapFeatures;

        g_VkPhysicalDevices[i].deviceVulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        g_VkPhysicalDevices[i].deviceVulkan13Features.pNext = (void*)&g_VkPhysicalDevices[i].deviceVulkan14Features;

        g_VkPhysicalDevices[i].deviceVulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        g_VkPhysicalDevices[i].deviceVulkan12Features.pNext = (void*)&g_VkPhysicalDevices[i].deviceVulkan13Features;

        g_VkPhysicalDevices[i].deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        g_VkPhysicalDevices[i].deviceFeatures.pNext = (void*)&g_VkPhysicalDevices[i].deviceVulkan12Features;

        vkGetPhysicalDeviceProperties2(physicalDevices[i], &g_VkPhysicalDevices[i].deviceProperties);
        vkGetPhysicalDeviceFeatures2(physicalDevices[i], &g_VkPhysicalDevices[i].deviceFeatures);
        vkGetPhysicalDeviceMemoryProperties(physicalDevices[i], &g_VkPhysicalDevices[i].deviceMemoryProperties);

        uint32_t queueFamilyPropertyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyPropertyCount, nullptr);
        g_VkPhysicalDevices[i].deviceQueueFamilyProperties.resize(queueFamilyPropertyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyPropertyCount, g_VkPhysicalDevices[i].deviceQueueFamilyProperties.data());

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevices[i], g_VkPresentationSurface, &g_VkPhysicalDevices[i].deviceSurfaceCaps);

        uint32_t surfaceFormatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevices[i], g_VkPresentationSurface, &surfaceFormatCount, nullptr);
        g_VkPhysicalDevices[i].deviceSurfaceFormats.resize(surfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevices[i], g_VkPresentationSurface, &surfaceFormatCount, g_VkPhysicalDevices[i].deviceSurfaceFormats.data());

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevices[i], g_VkPresentationSurface, &presentModeCount, nullptr);
        g_VkPhysicalDevices[i].deviceSurfacePresentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevices[i], g_VkPresentationSurface, &presentModeCount, g_VkPhysicalDevices[i].deviceSurfacePresentModes.data());
    }

    // Do some validation and select a desired physical device
    size_t selectedPhysicalDevice = UINT32_MAX;
    g_VkQueueFamilyIndex = UINT32_MAX;

    for (size_t i = 0; i < physicalDevices.size(); i++)
    {
        const PhysicalGfxDeviceInfo& physicalDevice = g_VkPhysicalDevices[i];
        const VkPhysicalDeviceRayTracingPipelineFeaturesKHR& rayTracingPipelineFeatures = physicalDevice.deviceRayTracingPipelineFeatures;

		if (physicalDevice.deviceProperties.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			continue;

		if (physicalDevice.deviceProperties.properties.apiVersion < VK_API_VERSION_1_4)
			continue;

        if (rayTracingPipelineFeatures.rayTracingPipeline == VK_FALSE)
            continue;

        for (size_t j = 0; j < physicalDevice.deviceQueueFamilyProperties.size(); j++)
        {
            const VkQueueFamilyProperties& vkQueueFamilyProperties = physicalDevice.deviceQueueFamilyProperties[j];

            VkBool32 supportsWin32Presentation = VK_FALSE;
            supportsWin32Presentation = vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDevice.device, (uint32_t)j);
            if (supportsWin32Presentation == VK_TRUE && vkQueueFamilyProperties.queueCount && (vkQueueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                g_VkQueueFamilyIndex = (uint32_t)j;
                break;
            }
        }

        if (g_VkQueueFamilyIndex != UINT32_MAX)
        {
            selectedPhysicalDevice = i;
            break;
        }
    }

    if ((selectedPhysicalDevice == UINT32_MAX) || (g_VkQueueFamilyIndex == UINT32_MAX))
    {
        MessageBox(hWnd, "No Vulkan 1.4 GPU with VK_KHR_ray_tracing_pipeline and VK_EXT_descriptor_heap found. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    const PhysicalGfxDeviceInfo& physicalDevice = g_VkPhysicalDevices[selectedPhysicalDevice];

    if (!physicalDevice.deviceDescriptorHeapFeatures.descriptorHeap ||
		!physicalDevice.deviceRayTracingPipelineFeatures.rayTracingPipeline ||
		!physicalDevice.deviceAccelerationStructureFeatures.accelerationStructure ||
        !physicalDevice.deviceVulkan12Features.bufferDeviceAddress ||
		!physicalDevice.deviceVulkan12Features.scalarBlockLayout ||
		!physicalDevice.deviceVulkan13Features.synchronization2 ||
		!physicalDevice.deviceVulkan14Features.maintenance5)
    {
        MessageBox(hWnd, "The selected GPU is missing a required feature. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    // Enumerate device extensions
    uint32_t deviceExtensionsCount = 0;
    if ((vkEnumerateDeviceExtensionProperties(g_VkPhysicalDevices[selectedPhysicalDevice].device, nullptr, &deviceExtensionsCount, nullptr) != VK_SUCCESS) || (deviceExtensionsCount == 0))
    {
        MessageBox(hWnd, "Couldn't enumerate Vulkan device extensions. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    g_VkDeviceExtensions.resize(deviceExtensionsCount);

    if (vkEnumerateDeviceExtensionProperties(g_VkPhysicalDevices[selectedPhysicalDevice].device, nullptr, &deviceExtensionsCount, g_VkDeviceExtensions.data()) != VK_SUCCESS)
    {
        MessageBox(hWnd, "Couldn't enumerate Vulkan device extensions. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    for (size_t i = 0; i < kMandatoryDeviceExtensions.size(); i++)
    {
        if (IsExtensionAvailable(g_VkDeviceExtensions, kMandatoryDeviceExtensions[i]) == false)
        {
            MessageBox(hWnd, "One or more required Vulkan device extensions is not supported. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
            return false;
        }
    }

    float queuePriorities = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo =
    {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        nullptr,
        0,
        g_VkQueueFamilyIndex,
        1,
        &queuePriorities
    };

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeature = {};
    accelStructFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelStructFeature.accelerationStructure = true;
	accelStructFeature.accelerationStructureCaptureReplay = physicalDevice.deviceAccelerationStructureFeatures.accelerationStructureCaptureReplay;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeature = {};
    rayTracingPipelineFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayTracingPipelineFeature.pNext = (void*)&accelStructFeature;
    rayTracingPipelineFeature.rayTracingPipeline = true;
	rayTracingPipelineFeature.rayTracingPipelineShaderGroupHandleCaptureReplay = physicalDevice.deviceRayTracingPipelineFeatures.rayTracingPipelineShaderGroupHandleCaptureReplay;

    VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptorHeapFeature = {};
    descriptorHeapFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
    descriptorHeapFeature.pNext = (void*)&rayTracingPipelineFeature;
    descriptorHeapFeature.descriptorHeap = true;
	descriptorHeapFeature.descriptorHeapCaptureReplay = physicalDevice.deviceDescriptorHeapFeatures.descriptorHeapCaptureReplay;

    VkPhysicalDeviceVulkan14Features vulkan14Features = {};
    vulkan14Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    vulkan14Features.pNext = (void*)&descriptorHeapFeature;
    vulkan14Features.maintenance5 = true;

    VkPhysicalDeviceVulkan13Features vulkan13Features = {};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.pNext = (void*)&vulkan14Features;
    vulkan13Features.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features vulkan12Features = {};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.pNext = (void*)&vulkan13Features;
    vulkan12Features.bufferDeviceAddress = true;
    vulkan12Features.scalarBlockLayout = true;

    VkPhysicalDeviceFeatures2 enabledFeatures = {};
    enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enabledFeatures.features.robustBufferAccess = true;
    enabledFeatures.pNext = (void*)&vulkan12Features;

    VkDeviceCreateInfo deviceCreateInfo =
    {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        nullptr,
        0,
        1,
        &queueCreateInfo,
        0,
        nullptr,
        (uint32_t)kMandatoryDeviceExtensions.size(),
        kMandatoryDeviceExtensions.data(),
        nullptr
    };

    deviceCreateInfo.pNext = (void*)&enabledFeatures;

    if (vkCreateDevice(g_VkPhysicalDevices[selectedPhysicalDevice].device, &deviceCreateInfo, nullptr, &g_VkDevice) != VK_SUCCESS)
    {
        MessageBox(hWnd, "Couldn't create Vulkan device. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

#define VULKAN_LOAD_DEVICE_FUNCTION(f) LoadDeviceFunction(f, #f, g_VkDevice);

    VULKAN_DEVICE_FUNCTION_LIST(VULKAN_LOAD_DEVICE_FUNCTION);

    vkGetDeviceQueue(g_VkDevice, g_VkQueueFamilyIndex, 0, &g_VkQueue);

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = g_VkQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(g_VkDevice, &poolInfo, nullptr, &g_VkCommandPool) != VK_SUCCESS)
        return false;

    VkSurfaceCapabilitiesKHR surfProperties;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_VkPhysicalDevices[selectedPhysicalDevice].device, g_VkPresentationSurface, &surfProperties);

    VkBool32 surfaceSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_VkPhysicalDevices[selectedPhysicalDevice].device, g_VkQueueFamilyIndex, g_VkPresentationSurface, &surfaceSupported);
    if (!surfaceSupported)
        return false;

    g_MaxFramesInFlight = std::max<unsigned int>(surfProperties.minImageCount, g_MaxFramesInFlight);

    // maxImageCount == 0 means the surface has no upper limit.
    if (surfProperties.maxImageCount > 0)
        g_MaxFramesInFlight = std::min<unsigned int>(surfProperties.maxImageCount, g_MaxFramesInFlight);

    g_CurrentPhysicalDevice = selectedPhysicalDevice;

    g_VkCommandBuffers.resize(g_MaxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = g_VkCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)g_VkCommandBuffers.size();

    if (vkAllocateCommandBuffers(g_VkDevice, &allocInfo, g_VkCommandBuffers.data()) != VK_SUCCESS)
        return false;

    if (!CreateSwapChain())
    {
        MessageBox(hWnd, "Couldn't create Vulkan swap chain. Exiting.", "Error", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    g_VkImageAvailableSemaphore.resize(g_MaxFramesInFlight);
    g_VkFrameFence.resize(g_MaxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < g_MaxFramesInFlight; i++)
    {
        if (vkCreateSemaphore(g_VkDevice, &semaphoreInfo, nullptr, &g_VkImageAvailableSemaphore[i]) != VK_SUCCESS)
            return false;

        if (vkCreateFence(g_VkDevice, &fenceInfo, nullptr, &g_VkFrameFence[i]) != VK_SUCCESS)
            return false;
    }

    return true;
}

void ShutdownVulkan()
{
    if (g_VkDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(g_VkDevice);
    }

    vkDestroyCommandPool(g_VkDevice, g_VkCommandPool, nullptr);

    if (g_VkSwapChain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(g_VkDevice, g_VkSwapChain, NULL);
        g_VkSwapChain = VK_NULL_HANDLE;
    }

    if (g_VkPresentationSurface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(g_VkInstance, g_VkPresentationSurface, nullptr);
        g_VkPresentationSurface = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < g_VkImageAvailableSemaphore.size(); i++)
        vkDestroySemaphore(g_VkDevice, g_VkImageAvailableSemaphore[i], nullptr);

    for (size_t i = 0; i < g_VkRenderingFinishedSemaphore.size(); i++)
        vkDestroySemaphore(g_VkDevice, g_VkRenderingFinishedSemaphore[i], nullptr);

    for (size_t i = 0; i < g_VkFrameFence.size(); i++)
        vkDestroyFence(g_VkDevice, g_VkFrameFence[i], nullptr);

    g_VkImageAvailableSemaphore.clear();
    g_VkRenderingFinishedSemaphore.clear();
    g_VkFrameFence.clear();

    if (g_VkDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(g_VkDevice);
        vkDestroyDevice(g_VkDevice, nullptr);
        g_VkDevice = VK_NULL_HANDLE;
    }

#if _DEBUG
#ifdef VK_EXT_debug_utils
    if (g_VkDebugUtilsMessenger != VK_NULL_HANDLE)
    {
        vkDestroyDebugUtilsMessengerEXT(g_VkInstance, g_VkDebugUtilsMessenger, nullptr);
        g_VkDebugUtilsMessenger = VK_NULL_HANDLE;
    }
#endif
#endif

    if (g_VkInstance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(g_VkInstance, nullptr);
        g_VkInstance = VK_NULL_HANDLE;
    }

    FreeLibrary(g_VulkanLib);
    g_VulkanLib = NULL;
}

VkCommandBuffer BeginOneTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = g_VkCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(g_VkDevice, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void EndOneTimeCommands(VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence;
    vkCreateFence(g_VkDevice, &fenceInfo, nullptr, &fence);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(g_VkQueue, 1, &submitInfo, fence);
    vkWaitForFences(g_VkDevice, 1, &fence, VK_TRUE, (uint64_t)-1);

    vkDestroyFence(g_VkDevice, fence, nullptr);
    vkFreeCommandBuffers(g_VkDevice, g_VkCommandPool, 1, &commandBuffer);
}
