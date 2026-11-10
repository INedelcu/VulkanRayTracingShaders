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

enum class MaterialType : uint32_t
{
    Diffuse,
    Glass,
};

// Per instance hit record
struct HitRecord
{
    VkDeviceAddress vertices;
    VkDeviceAddress indices;
    shared::MaterialConstants material;
};

// One per instance, in instance order. The material type picks the radiance and shadow hit groups.
struct InstanceHitRecord
{
    MaterialType material;
    HitRecord record;
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
    RayTracingShaderModule glass;   // ClosestHitGlass, AnyHitGlassShadow
};

// Global bindings (global root signature in DXR)
VkDescriptorSetAndBindingMappingEXT HeapConstantOffsetMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, VkDeviceSize heapOffset);
VkDescriptorSetAndBindingMappingEXT PushDataMapping(uint32_t set, uint32_t binding, uint32_t pushOffset);
VkDescriptorSetAndBindingMappingEXT PushAddressMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t pushOffset);

// Local bindings for each shader record (local root signature in DXR)
VkDescriptorSetAndBindingMappingEXT ShaderRecordDataMapping(uint32_t set, uint32_t binding, uint32_t recordOffset);
VkDescriptorSetAndBindingMappingEXT ShaderRecordAddressMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t recordOffset);

bool CreateRayTracingPipeline(RayTracingPipeline& rtPipeline, const RayTracingShaders& shaders);
bool CreateShaderBindingTable(RayTracingPipeline& rtPipeline, const std::vector<InstanceHitRecord>& hitRecords);
void DestroyRayTracingPipeline(RayTracingPipeline& rtPipeline);
