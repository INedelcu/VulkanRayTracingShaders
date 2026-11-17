#pragma once

#include "DescriptorHeap.h"
#include "VulkanBuffers.h"
#include "MathUtils.h"
#include "RayTracingMaterial.h"
#include "RayTracingPipeline.h"

#include <vector>

#ifdef VK_KHR_acceleration_structure
struct AccelerationStructure
{
    VkAccelerationStructureKHR handle  = VK_NULL_HANDLE;
    Buffer                     buffer;
    VkDeviceAddress            address = 0;
};

extern AccelerationStructure g_Tlas;

// Loads the surface shaders, whose global resources are bound as globals says, and creates the scene that uses
// them. Its textures take slots of the resource heap, which is where their descriptors go.
bool CreateRayTracingScene(const RayTracingGlobalResources& globals, DescriptorHeap& resourceHeap);
void DestroyRayTracingScene();

void GetHitRecords(RayTracingHitRecords& hitRecords);

void BuildTLAS(VkCommandBuffer commandBuffer, size_t frame);
#endif
