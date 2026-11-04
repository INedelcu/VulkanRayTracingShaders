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
struct MeshRecord
{
    VkDeviceAddress vertices;
    VkDeviceAddress indices;
};

VkDescriptorSetAndBindingMappingEXT PushDataMapping(uint32_t set, uint32_t binding, uint32_t pushOffset);
VkDescriptorSetAndBindingMappingEXT ShaderRecordAddressMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t recordOffset);
VkDescriptorSetAndBindingMappingEXT PushAddressMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t pushOffset);

bool CreateRayTracingPipeline(RayTracingPipeline& rtPipeline, VkShaderModule shaderModule, const std::vector<VkDescriptorSetAndBindingMappingEXT>& mappings);
bool CreateShaderBindingTable(RayTracingPipeline& rtPipeline, const std::vector<MeshRecord>& hitRecords);
void DestroyRayTracingPipeline(RayTracingPipeline& rtPipeline);
