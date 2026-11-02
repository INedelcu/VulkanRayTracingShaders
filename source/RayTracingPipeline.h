#pragma once

#include "VulkanBuffers.h"

#include <vector>

struct RayTracingPipeline
{
    VkPipeline                      pipeline = VK_NULL_HANDLE;
    Buffer                          sbtBuffer;
    VkStridedDeviceAddressRegionKHR raygenRegion = {};
    VkStridedDeviceAddressRegionKHR missRegion = {};
    VkStridedDeviceAddressRegionKHR hitRegion = {};
    VkStridedDeviceAddressRegionKHR callableRegion = {};
};

VkDescriptorSetAndBindingMappingEXT HeapConstantOffsetMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, VkDeviceSize heapOffset);
VkDescriptorSetAndBindingMappingEXT PushAddressMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t pushOffset);

bool CreateRayTracingPipeline(RayTracingPipeline& rtPipeline, VkShaderModule shaderModule, const std::vector<VkDescriptorSetAndBindingMappingEXT>& mappings);
void DestroyRayTracingPipeline(RayTracingPipeline& rtPipeline);
