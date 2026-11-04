#include "RayTracingPipeline.h"
#include "SharedTypes.h"

#include <cstring>

enum ShaderStage : uint32_t { StageRayGen, StageMissRadiance, StageMissShadow, StageClosestHit, StageCount };
enum ShaderGroup : uint32_t { GroupRayGen, GroupMissRadiance, GroupMissShadow, GroupHitRadiance, GroupHitShadow, GroupCount };

static VkDescriptorSetAndBindingMappingEXT MakeMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, VkDescriptorMappingSourceEXT source)
{
    VkDescriptorSetAndBindingMappingEXT mapping = {};
    mapping.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
    mapping.descriptorSet = set;
    mapping.firstBinding = binding;
    mapping.bindingCount = 1;
    mapping.resourceMask = resourceMask;
    mapping.source = source;

    return mapping;
}

VkDescriptorSetAndBindingMappingEXT HeapConstantOffsetMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, VkDeviceSize heapOffset)
{
    VkDescriptorSetAndBindingMappingEXT mapping = MakeMapping(set, binding, resourceMask, VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT);
    mapping.sourceData.constantOffset.heapOffset = (uint32_t)heapOffset;

    return mapping;
}

VkDescriptorSetAndBindingMappingEXT PushDataMapping(uint32_t set, uint32_t binding, uint32_t pushOffset)
{
    VkDescriptorSetAndBindingMappingEXT mapping = MakeMapping(set, binding, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT, VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_DATA_EXT);
    mapping.sourceData.pushDataOffset = pushOffset;

    return mapping;
}

VkDescriptorSetAndBindingMappingEXT ShaderRecordAddressMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t recordOffset)
{
    VkDescriptorSetAndBindingMappingEXT mapping = MakeMapping(set, binding, resourceMask, VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_ADDRESS_EXT);
    mapping.sourceData.shaderRecordAddressOffset = recordOffset;

    return mapping;
}

VkDescriptorSetAndBindingMappingEXT PushAddressMapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t pushOffset)
{
    VkDescriptorSetAndBindingMappingEXT mapping = MakeMapping(set, binding, resourceMask, VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT);
    mapping.sourceData.pushAddressOffset = pushOffset;

    return mapping;
}

static VkRayTracingShaderGroupCreateInfoKHR GeneralGroup(uint32_t stage)
{
    VkRayTracingShaderGroupCreateInfoKHR group = {};
    group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = stage;
    group.closestHitShader = VK_SHADER_UNUSED_KHR;
    group.anyHitShader = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;

    return group;
}

static VkRayTracingShaderGroupCreateInfoKHR TrianglesGroup(uint32_t closestHitStage)
{
    VkRayTracingShaderGroupCreateInfoKHR group = {};
    group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = closestHitStage;
    group.anyHitShader = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;

    return group;
}

bool CreateShaderBindingTable(RayTracingPipeline& rtPipeline, const std::vector<MeshRecord>& hitRecords)
{
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rtpp = g_VkPhysicalDevices[g_CurrentPhysicalDevice].deviceRayTracingPipelineProperties;

    const uint32_t handleSize = rtpp.shaderGroupHandleSize;
    std::vector<uint8_t> handles(GroupCount * handleSize);

    if (vkGetRayTracingShaderGroupHandlesKHR(g_VkDevice, rtPipeline.pipeline, 0, GroupCount, handles.size(), handles.data()) != VK_SUCCESS)
        return false;

    // The raygen and miss records (one miss record per ray type) hold only shader handles. Each instance has one hit record per ray type: the hit group handle followed by a MeshRecord with the instance's vertex and index buffer addresses.

    const VkDeviceSize raygenStride = AlignUp(handleSize, rtpp.shaderGroupBaseAlignment);
    const VkDeviceSize missStride = AlignUp(handleSize, rtpp.shaderGroupHandleAlignment);
    const VkDeviceSize hitStride = AlignUp(handleSize + sizeof(MeshRecord), rtpp.shaderGroupHandleAlignment);

    if (hitRecords.empty() || hitStride > rtpp.maxShaderGroupStride)
        return false;

    const VkDeviceSize hitCount = hitRecords.size() * shared::RAY_TYPE_COUNT;

    const VkDeviceSize raygenOffset = 0;
    const VkDeviceSize missOffset = AlignUp(raygenOffset + raygenStride, rtpp.shaderGroupBaseAlignment);
    const VkDeviceSize hitOffset = AlignUp(missOffset + missStride * shared::RAY_TYPE_COUNT, rtpp.shaderGroupBaseAlignment);
    const VkDeviceSize tableSize = hitOffset + hitStride * hitCount;

    rtPipeline.sbtBuffer = CreateBuffer(tableSize + rtpp.shaderGroupBaseAlignment,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "Shader binding table");

    if (rtPipeline.sbtBuffer.mapped == nullptr)
        return false;

    const VkDeviceAddress tableAddress = AlignUp(rtPipeline.sbtBuffer.address, rtpp.shaderGroupBaseAlignment);
    uint8_t* table = (uint8_t*)rtPipeline.sbtBuffer.mapped + (tableAddress - rtPipeline.sbtBuffer.address);

    ShaderGroup missGroups[shared::RAY_TYPE_COUNT];
    missGroups[shared::RAY_TYPE_RADIANCE] = GroupMissRadiance;
    missGroups[shared::RAY_TYPE_SHADOW] = GroupMissShadow;

    ShaderGroup hitGroups[shared::RAY_TYPE_COUNT];
    hitGroups[shared::RAY_TYPE_RADIANCE] = GroupHitRadiance;
    hitGroups[shared::RAY_TYPE_SHADOW] = GroupHitShadow;

    memcpy(table + raygenOffset, handles.data() + GroupRayGen * handleSize, handleSize);

    for (uint32_t rayType = 0; rayType < shared::RAY_TYPE_COUNT; rayType++)
        memcpy(table + missOffset + rayType * missStride, handles.data() + missGroups[rayType] * handleSize, handleSize);

    for (size_t i = 0; i < hitRecords.size(); i++)
    {
        for (uint32_t rayType = 0; rayType < shared::RAY_TYPE_COUNT; rayType++)
        {
            uint8_t* record = table + hitOffset + (i * shared::RAY_TYPE_COUNT + rayType) * hitStride;
            memcpy(record, handles.data() + hitGroups[rayType] * handleSize, handleSize);
            memcpy(record + handleSize, &hitRecords[i], sizeof(MeshRecord));
        }
    }

    rtPipeline.raygenRegion = { tableAddress + raygenOffset, raygenStride, raygenStride };
    rtPipeline.missRegion = { tableAddress + missOffset, missStride, missStride * shared::RAY_TYPE_COUNT };
    rtPipeline.hitRegion = { tableAddress + hitOffset, hitStride, hitStride * hitCount };
    rtPipeline.callableRegion = {};

    return true;
}

bool CreateRayTracingPipeline(RayTracingPipeline& rtPipeline, VkShaderModule shaderModule, const std::vector<VkDescriptorSetAndBindingMappingEXT>& mappings)
{
    VkShaderDescriptorSetAndBindingMappingInfoEXT mappingInfo = {};
    mappingInfo.sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT;
    mappingInfo.mappingCount = (uint32_t)mappings.size();
    mappingInfo.pMappings = mappings.data();

    const VkShaderStageFlagBits stageFlags[StageCount] = { VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
    const char* entryPoints[StageCount] = { "RayGen", "MissRadiance", "MissShadow", "ClosestHit" };

    VkPipelineShaderStageCreateInfo stages[StageCount] = {};

    for (uint32_t i = 0; i < StageCount; i++)
    {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].pNext = &mappingInfo;
        stages[i].stage = stageFlags[i];
        stages[i].module = shaderModule;
        stages[i].pName = entryPoints[i];
    }

    VkRayTracingShaderGroupCreateInfoKHR groups[GroupCount];
    groups[GroupRayGen] = GeneralGroup(StageRayGen);
    groups[GroupMissRadiance] = GeneralGroup(StageMissRadiance);
    groups[GroupMissShadow] = GeneralGroup(StageMissShadow);
    groups[GroupHitRadiance] = TrianglesGroup(StageClosestHit);
    groups[GroupHitShadow] = TrianglesGroup(VK_SHADER_UNUSED_KHR);

    VkPipelineCreateFlags2CreateInfo flags2 = {};
    flags2.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO;
    flags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

    VkRayTracingPipelineCreateInfoKHR pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineInfo.pNext = &flags2;
    pipelineInfo.stageCount = StageCount;
    pipelineInfo.pStages = stages;
    pipelineInfo.groupCount = GroupCount;
    pipelineInfo.pGroups = groups;
    pipelineInfo.maxPipelineRayRecursionDepth = 1;
    pipelineInfo.layout = VK_NULL_HANDLE;

    if (vkCreateRayTracingPipelinesKHR(g_VkDevice, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rtPipeline.pipeline) != VK_SUCCESS)
    {
        rtPipeline.pipeline = VK_NULL_HANDLE;
        return false;
    }

    SetVkObjectName((uint64_t)rtPipeline.pipeline, VK_OBJECT_TYPE_PIPELINE, "Ray Tracing Pipeline");

    return true;
}

void DestroyRayTracingPipeline(RayTracingPipeline& rtPipeline)
{
    if (rtPipeline.pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(g_VkDevice, rtPipeline.pipeline, nullptr);

    DestroyBuffer(rtPipeline.sbtBuffer);

    rtPipeline = {};
}
