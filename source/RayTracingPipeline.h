#pragma once

#include "RayTracingShader.h"
#include "VulkanBuffers.h"

#include <vector>

// What the hit records of the TLAS instances hold, in instance order: the surface shader each instance uses and the
// local data its hit shaders read. An instance without a surface shader has a null one and no local data.
class RayTracingHitRecords
{
public:
    void Clear();

    // Adds the next instance. localData holds shader->LocalDataSize() bytes and is copied; it is not read when the
    // shader is null.
    void Add(const RayTracingSurfaceShader* shader, const uint8_t* localData);

    size_t Count() const { return m_Shaders.size(); }

    const RayTracingSurfaceShader* Shader(size_t instance) const { return m_Shaders[instance]; }
    const uint8_t*                 LocalData(size_t instance) const { return m_LocalData.data() + m_LocalDataOffsets[instance]; }

    bool operator==(const RayTracingHitRecords& other) const = default;

private:
    std::vector<const RayTracingSurfaceShader*> m_Shaders;
    std::vector<size_t>                         m_LocalDataOffsets;
    std::vector<uint8_t>                        m_LocalData;
};

// The ray tracing pipeline and its shader binding table, made from the ray generation shader and what the TLAS
// instances use. Both are kept from frame to frame and remade only when they no longer fit the scene.
class RayTracingPipeline
{
public:
    // Call each frame before recording commands. The pipeline is recreated only when an instance uses a surface shader
    // the pipeline doesn't have, and the shader binding table only when the pipeline or a record changed; otherwise
    // this only compares the records with the ones the table was built from. False when either can't be created.
    bool Update(const RayTracingRayGenShader& rayGen, const RayTracingHitRecords& hitRecords);
    void Destroy();

    void Bind(VkCommandBuffer commandBuffer) const;
    void TraceRays(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height) const;

private:
    bool HasShaders(const RayTracingRayGenShader& rayGen, const RayTracingHitRecords& hitRecords) const;
    bool CreatePipeline(const RayTracingRayGenShader& rayGen, const RayTracingHitRecords& hitRecords);
    bool CreateShaderBindingTable(const RayTracingHitRecords& hitRecords);
    void DestroyPipeline();
    void DestroyShaderBindingTable();

    uint32_t HitGroupIndex(const RayTracingSurfaceShader* shader, uint32_t rayType) const;

    // The pipeline and the shaders it was created from. Its shader groups are the ray generation shader, a miss shader
    // per ray type, then for each surface shader a hit group per ray type.
    VkPipeline                                  m_Pipeline = VK_NULL_HANDLE;
    const RayTracingRayGenShader*               m_RayGenShader = nullptr;
    std::vector<const RayTracingSurfaceShader*> m_SurfaceShaders;

    // The shader binding table and the data it was built from.
    Buffer                          m_SbtBuffer;
    std::vector<uint8_t>            m_RayGenLocalData;
    RayTracingHitRecords            m_HitRecords;
    VkStridedDeviceAddressRegionKHR m_RayGenRegion = {};
    VkStridedDeviceAddressRegionKHR m_MissRegion = {};
    VkStridedDeviceAddressRegionKHR m_HitRegion = {};
    VkStridedDeviceAddressRegionKHR m_CallableRegion = {};
};
