#pragma once

#include "VulkanBuffers.h"
#include "MathUtils.h"

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
    uint32_t  mesh = 0;
    Transform transform;
};

void CreateRayTracingScene();
void DestroyRayTracingScene();

void BuildTLAS(VkCommandBuffer commandBuffer, size_t frame);
#endif
