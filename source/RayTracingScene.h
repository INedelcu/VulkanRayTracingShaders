#pragma once

#include "VulkanBuffers.h"
#include "MathUtils.h"
#include "RayTracingPipeline.h"

#include <vector>

#ifdef VK_KHR_acceleration_structure
struct AccelerationStructure
{
    VkAccelerationStructureKHR handle  = VK_NULL_HANDLE;
    Buffer                     buffer;
    VkDeviceAddress            address = 0;
};

struct Mesh
{
    Buffer                vertexBuffer;
    Buffer                indexBuffer;
    uint32_t              vertexCount = 0;
    uint32_t              indexCount  = 0;
    AccelerationStructure blas;
};

struct Instance
{
    uint32_t     mesh = 0;
    Transform    transform;
    MaterialType material = MaterialType::Diffuse;
    Vec3         color = { 1.0f, 1.0f, 1.0f };
    float        ior = 1.5f;
    float        extinction = 1.0f; // glass tint per unit crossed: 0 = clear, 1 = a color channel at 0 keeps 37%, 3 = keeps 5%
    float        roughness = 1.0f; // 1 is matte, 0 a mirror
};

extern std::vector<Mesh>     g_Meshes;
extern std::vector<Instance> g_Instances;
extern AccelerationStructure g_Tlas;

void CreateRayTracingScene();
void DestroyRayTracingScene();

void BuildTLAS(VkCommandBuffer commandBuffer, size_t frame);
#endif
