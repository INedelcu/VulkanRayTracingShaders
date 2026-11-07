#pragma once

#include "VulkanBuffers.h"
#include "SharedTypes.h"

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

// Per instance hit record
struct HitRecord
{
    VkDeviceAddress vertices;
    VkDeviceAddress indices;
    shared::MaterialConstants material;
};

struct RayTracingShaderModule
{
    VkShaderModule module = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetAndBindingMappingEXT> mappings;
};

struct RayTracingShaders
{
    RayTracingShaderModule rayGen;  // RayGen, MissRadiance, MissShadow
    RayTracingShaderModule diffuse; // ClosestHitDiffuse
};

// Global bindings (global root signature in DXR)
VkDescriptorSetAndBindingMappingEXT HeapConstantOffsetMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, VkDeviceSize heapOffset);
VkDescriptorSetAndBindingMappingEXT PushDataMapping(uint32_t set, uint32_t binding, uint32_t pushOffset);
VkDescriptorSetAndBindingMappingEXT PushAddressMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t pushOffset);

// Local bindings for each shader record (local root signature in DXR)
VkDescriptorSetAndBindingMappingEXT ShaderRecordDataMapping(uint32_t set, uint32_t binding, uint32_t recordOffset);
VkDescriptorSetAndBindingMappingEXT ShaderRecordAddressMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t recordOffset);

bool CreateRayTracingPipeline(RayTracingPipeline& rtPipeline, const RayTracingShaders& shaders);
bool CreateShaderBindingTable(RayTracingPipeline& rtPipeline, const std::vector<HitRecord>& hitRecords);
void DestroyRayTracingPipeline(RayTracingPipeline& rtPipeline);
