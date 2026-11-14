#include "RayTracingPipeline.h"
#include "SharedTypes.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

static const uint32_t kRayGenGroup = 0;
static const uint32_t kFirstMissGroup = 1;
static const uint32_t kFirstHitGroup = kFirstMissGroup + shared::RAY_TYPE_COUNT;

// One compiled entry point of the pipeline.
struct Stage
{
    const RayTracingShader* shader;
    VkShaderStageFlagBits   stage;
    const std::string*      entryPoint;
};

// The stage's index, or VK_SHADER_UNUSED_KHR for a shader left out of its group.
static uint32_t AddStage(std::vector<Stage>& stages, const RayTracingShader& shader, VkShaderStageFlagBits stage, const std::string& entryPoint)
{
    if (entryPoint.empty())
        return VK_SHADER_UNUSED_KHR;

    stages.push_back({ &shader, stage, &entryPoint });

    return (uint32_t)stages.size() - 1;
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

static VkRayTracingShaderGroupCreateInfoKHR TrianglesGroup(uint32_t closestHitStage, uint32_t anyHitStage)
{
    VkRayTracingShaderGroupCreateInfoKHR group = {};
    group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = closestHitStage;
    group.anyHitShader = anyHitStage;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;

    return group;
}

void RayTracingHitRecords::Clear()
{
    m_Shaders.clear();
    m_LocalDataOffsets.clear();
    m_LocalData.clear();
}

void RayTracingHitRecords::Add(const RayTracingSurfaceShader* shader, const uint8_t* localData)
{
    m_Shaders.push_back(shader);
    m_LocalDataOffsets.push_back(m_LocalData.size());

    if (shader != nullptr)
        m_LocalData.insert(m_LocalData.end(), localData, localData + shader->LocalDataSize());
}

bool RayTracingPipeline::Update(const RayTracingRayGenShader& rayGen, const RayTracingHitRecords& hitRecords)
{
    if (hitRecords.Count() == 0)
        return false;

    const bool pipelineValid = HasShaders(rayGen, hitRecords);
    const bool tableValid = pipelineValid && m_SbtBuffer.buffer != VK_NULL_HANDLE && rayGen.LocalData() == m_RayGenLocalData && hitRecords == m_HitRecords;

    if (tableValid)
        return true;

    // Frames in flight may still be tracing with the pipeline and the table.
    vkDeviceWaitIdle(g_VkDevice);

    if (!pipelineValid)
    {
        DestroyPipeline();

        if (!CreatePipeline(rayGen, hitRecords))
            return false;
    }

    DestroyShaderBindingTable();

    return CreateShaderBindingTable(hitRecords);
}

// Whether the pipeline was created from this ray generation shader and has every surface shader the instances use.
// It may have more: surface shaders no instance uses any longer stay in it until it is recreated. Instances without a
// surface shader need nothing from it.
bool RayTracingPipeline::HasShaders(const RayTracingRayGenShader& rayGen, const RayTracingHitRecords& hitRecords) const
{
    if (m_Pipeline == VK_NULL_HANDLE || m_RayGenShader != &rayGen)
        return false;

    for (size_t i = 0; i < hitRecords.Count(); i++)
        if (hitRecords.Shader(i) != nullptr && std::find(m_SurfaceShaders.begin(), m_SurfaceShaders.end(), hitRecords.Shader(i)) == m_SurfaceShaders.end())
            return false;

    return true;
}

bool RayTracingPipeline::CreatePipeline(const RayTracingRayGenShader& rayGen, const RayTracingHitRecords& hitRecords)
{
    // The surface shaders the instances use, each once, in the order of their first use.
    std::vector<const RayTracingSurfaceShader*> surfaceShaders;

    for (size_t i = 0; i < hitRecords.Count(); i++)
        if (hitRecords.Shader(i) != nullptr && std::find(surfaceShaders.begin(), surfaceShaders.end(), hitRecords.Shader(i)) == surfaceShaders.end())
            surfaceShaders.push_back(hitRecords.Shader(i));

    std::vector<Stage> stages;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;

    groups.push_back(GeneralGroup(AddStage(stages, rayGen, VK_SHADER_STAGE_RAYGEN_BIT_KHR, rayGen.RayGenEntryPoint())));

    for (uint32_t rayType = 0; rayType < shared::RAY_TYPE_COUNT; rayType++)
        groups.push_back(GeneralGroup(AddStage(stages, rayGen, VK_SHADER_STAGE_MISS_BIT_KHR, rayGen.MissEntryPoint(rayType))));

    for (const RayTracingSurfaceShader* shader : surfaceShaders)
    {
        for (uint32_t rayType = 0; rayType < shared::RAY_TYPE_COUNT; rayType++)
        {
            const RayTracingHitGroup& hitGroup = shader->HitGroup(rayType);

            const uint32_t closestHit = AddStage(stages, *shader, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, hitGroup.closestHit);
            const uint32_t anyHit = AddStage(stages, *shader, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, hitGroup.anyHit);

            groups.push_back(TrianglesGroup(closestHit, anyHit));
        }
    }

    // Each module has its own bindings, so each stage gets the mappings of its own shader.
    std::vector<VkShaderDescriptorSetAndBindingMappingInfoEXT> mappingInfos(stages.size());
    std::vector<VkPipelineShaderStageCreateInfo> stageInfos(stages.size());

    for (size_t i = 0; i < stages.size(); i++)
    {
        mappingInfos[i] = {};
        mappingInfos[i].sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT;
        mappingInfos[i].mappingCount = (uint32_t)stages[i].shader->Mappings().size();
        mappingInfos[i].pMappings = stages[i].shader->Mappings().data();

        stageInfos[i] = {};
        stageInfos[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfos[i].pNext = &mappingInfos[i];
        stageInfos[i].stage = stages[i].stage;
        stageInfos[i].module = stages[i].shader->Module();
        stageInfos[i].pName = stages[i].entryPoint->c_str();
    }

    VkPipelineCreateFlags2CreateInfo flags2 = {};
    flags2.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO;
    flags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

    VkRayTracingPipelineCreateInfoKHR pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineInfo.pNext = &flags2;
    pipelineInfo.stageCount = (uint32_t)stageInfos.size();
    pipelineInfo.pStages = stageInfos.data();
    pipelineInfo.groupCount = (uint32_t)groups.size();
    pipelineInfo.pGroups = groups.data();
    pipelineInfo.maxPipelineRayRecursionDepth = 1;
    pipelineInfo.layout = VK_NULL_HANDLE;

    if (vkCreateRayTracingPipelinesKHR(g_VkDevice, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS)
    {
        m_Pipeline = VK_NULL_HANDLE;
        return false;
    }

    SetVkObjectName((uint64_t)m_Pipeline, VK_OBJECT_TYPE_PIPELINE, "Ray Tracing Pipeline");

    m_RayGenShader = &rayGen;
    m_SurfaceShaders = surfaceShaders;

    char message[128];
    _snprintf_s(message, _TRUNCATE, "Ray tracing pipeline created: %zu surface shaders, %zu stages, %zu groups\n", surfaceShaders.size(), stages.size(), groups.size());
    OutputDebugString(message);

    return true;
}

uint32_t RayTracingPipeline::HitGroupIndex(const RayTracingSurfaceShader* shader, uint32_t rayType) const
{
    const size_t shaderIndex = std::find(m_SurfaceShaders.begin(), m_SurfaceShaders.end(), shader) - m_SurfaceShaders.begin();

    return kFirstHitGroup + (uint32_t)shaderIndex * shared::RAY_TYPE_COUNT + rayType;
}

bool RayTracingPipeline::CreateShaderBindingTable(const RayTracingHitRecords& hitRecords)
{
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rtpp = g_VkPhysicalDevices[g_CurrentPhysicalDevice].deviceRayTracingPipelineProperties;

    const uint32_t groupCount = kFirstHitGroup + (uint32_t)m_SurfaceShaders.size() * shared::RAY_TYPE_COUNT;
    const uint32_t handleSize = rtpp.shaderGroupHandleSize;
    std::vector<uint8_t> handles(groupCount * handleSize);

    if (vkGetRayTracingShaderGroupHandlesKHR(g_VkDevice, m_Pipeline, 0, groupCount, handles.size(), handles.data()) != VK_SUCCESS)
        return false;

    // Every record is a shader group handle followed by the local data of the group's shader. The ray generation
    // record and the miss records (one per ray type) carry the ray generation shader's. Each instance has one hit
    // record per ray type, carrying its surface shader's; the records share one stride, so they are as large as the
    // largest local data of the pipeline's surface shaders. The records of an instance without a surface shader hold
    // a handle of zeros, the null shader group: no hit shader runs, the hit only stops the ray.
    const std::vector<uint8_t>& rayGenLocalData = m_RayGenShader->LocalData();

    uint32_t hitLocalDataSize = 0;

    for (const RayTracingSurfaceShader* shader : m_SurfaceShaders)
        hitLocalDataSize = (std::max)(hitLocalDataSize, shader->LocalDataSize());

    const VkDeviceSize rayGenStride = AlignUp(handleSize + rayGenLocalData.size(), rtpp.shaderGroupBaseAlignment);
    const VkDeviceSize missStride = AlignUp(handleSize + rayGenLocalData.size(), rtpp.shaderGroupHandleAlignment);
    const VkDeviceSize hitStride = AlignUp(handleSize + hitLocalDataSize, rtpp.shaderGroupHandleAlignment);

    if ((std::max)({ rayGenStride, missStride, hitStride }) > rtpp.maxShaderGroupStride)
        return false;

    const VkDeviceSize hitCount = hitRecords.Count() * shared::RAY_TYPE_COUNT;

    const VkDeviceSize rayGenOffset = 0;
    const VkDeviceSize missOffset = AlignUp(rayGenOffset + rayGenStride, rtpp.shaderGroupBaseAlignment);
    const VkDeviceSize hitOffset = AlignUp(missOffset + missStride * shared::RAY_TYPE_COUNT, rtpp.shaderGroupBaseAlignment);
    const VkDeviceSize tableSize = hitOffset + hitStride * hitCount;

    m_SbtBuffer = CreateBuffer(tableSize + rtpp.shaderGroupBaseAlignment,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "Shader binding table");

    if (m_SbtBuffer.mapped == nullptr)
        return false;

    const VkDeviceAddress tableAddress = AlignUp(m_SbtBuffer.address, rtpp.shaderGroupBaseAlignment);
    uint8_t* table = (uint8_t*)m_SbtBuffer.mapped + (tableAddress - m_SbtBuffer.address);

    auto writeRecord = [&](VkDeviceSize offset, uint32_t group, const uint8_t* localData, size_t localDataSize)
    {
        memcpy(table + offset, handles.data() + group * handleSize, handleSize);

        if (localDataSize != 0)
            memcpy(table + offset + handleSize, localData, localDataSize);
    };

    writeRecord(rayGenOffset, kRayGenGroup, rayGenLocalData.data(), rayGenLocalData.size());

    for (uint32_t rayType = 0; rayType < shared::RAY_TYPE_COUNT; rayType++)
        writeRecord(missOffset + rayType * missStride, kFirstMissGroup + rayType, rayGenLocalData.data(), rayGenLocalData.size());

    for (size_t i = 0; i < hitRecords.Count(); i++)
    {
        const RayTracingSurfaceShader* shader = hitRecords.Shader(i);

        for (uint32_t rayType = 0; rayType < shared::RAY_TYPE_COUNT; rayType++)
        {
            const VkDeviceSize offset = hitOffset + (i * shared::RAY_TYPE_COUNT + rayType) * hitStride;

            if (shader != nullptr)
                writeRecord(offset, HitGroupIndex(shader, rayType), hitRecords.LocalData(i), shader->LocalDataSize());
            else
                memset(table + offset, 0, handleSize);
        }
    }

    m_RayGenRegion = { tableAddress + rayGenOffset, rayGenStride, rayGenStride };
    m_MissRegion = { tableAddress + missOffset, missStride, missStride * shared::RAY_TYPE_COUNT };
    m_HitRegion = { tableAddress + hitOffset, hitStride, hitStride * hitCount };
    m_CallableRegion = {};

    m_RayGenLocalData = rayGenLocalData;
    m_HitRecords = hitRecords;

    char message[128];
    _snprintf_s(message, _TRUNCATE, "Shader binding table created: %zu instances, %llu byte hit records\n", hitRecords.Count(), (unsigned long long)hitStride);
    OutputDebugString(message);

    return true;
}

void RayTracingPipeline::DestroyPipeline()
{
    if (m_Pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(g_VkDevice, m_Pipeline, nullptr);

    m_Pipeline = VK_NULL_HANDLE;
    m_RayGenShader = nullptr;
    m_SurfaceShaders.clear();
}

void RayTracingPipeline::DestroyShaderBindingTable()
{
    DestroyBuffer(m_SbtBuffer);

    m_RayGenLocalData.clear();
    m_HitRecords.Clear();
    m_RayGenRegion = {};
    m_MissRegion = {};
    m_HitRegion = {};
    m_CallableRegion = {};
}

void RayTracingPipeline::Destroy()
{
    DestroyPipeline();
    DestroyShaderBindingTable();
}

void RayTracingPipeline::Bind(VkCommandBuffer commandBuffer) const
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_Pipeline);
}

void RayTracingPipeline::TraceRays(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height) const
{
    vkCmdTraceRaysKHR(commandBuffer, &m_RayGenRegion, &m_MissRegion, &m_HitRegion, &m_CallableRegion, width, height, 1);
}
