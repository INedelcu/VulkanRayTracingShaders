#include "RayTracingScene.h"
#include "Geometry.h"

#include <cstring>
#include <iterator>
#include <vector>

#ifdef VK_KHR_acceleration_structure
enum MeshId : uint8_t
{
    MeshQuad = 0,
    MeshCube,
    MeshSphere,
    MeshTorus
};

std::vector<Mesh>       g_Meshes;
std::vector<Instance>   g_Instances;
std::vector<Buffer>     g_InstanceBuffers; // one per frame in flight, host visible so the CPU can rewrite it while older frames are still building
Buffer                  g_TlasScratch;
VkDeviceAddress         g_TlasScratchAddress = 0;
AccelerationStructure   g_Tlas;

static AccelerationStructure CreateAccelerationStructure(VkAccelerationStructureTypeKHR type, VkDeviceSize size, const char* name)
{
    AccelerationStructure accelerationStructure;
    accelerationStructure.buffer = CreateBuffer(size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, name);

    VkAccelerationStructureCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = accelerationStructure.buffer.buffer;
    createInfo.size = size;
    createInfo.type = type;

    vkCreateAccelerationStructureKHR(g_VkDevice, &createInfo, nullptr, &accelerationStructure.handle);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = accelerationStructure.handle;

    accelerationStructure.address = vkGetAccelerationStructureDeviceAddressKHR(g_VkDevice, &addressInfo);

    return accelerationStructure;
}

static void DestroyAccelerationStructure(AccelerationStructure& accelerationStructure)
{
    if (accelerationStructure.handle != VK_NULL_HANDLE)
        vkDestroyAccelerationStructureKHR(g_VkDevice, accelerationStructure.handle, nullptr);

    DestroyBuffer(accelerationStructure.buffer);
    accelerationStructure = {};
}

static void CreateMeshes()
{
    const VkBufferUsageFlags geometryUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

    const MeshData meshData[] = { CreateQuad(), CreateCube(), CreateUvSphere(128, 64), CreateTorus(1.0f, 0.32f, 256, 64) };
    const char* meshNames[] = { "Quad", "Cube", "Sphere", "Torus" };

    for (size_t i = 0; i < std::size(meshData); i++)
    {
        Mesh mesh;
        mesh.vertexCount = (uint32_t)meshData[i].vertices.size();
        mesh.indexCount = (uint32_t)meshData[i].indices.size();
        mesh.vertexBuffer = CreateBufferWithData(meshData[i].vertices.data(), mesh.vertexCount * sizeof(shared::Vertex), geometryUsage, meshNames[i]);
        mesh.indexBuffer = CreateBufferWithData(meshData[i].indices.data(), mesh.indexCount * sizeof(uint32_t), geometryUsage, meshNames[i]);

        g_Meshes.push_back(mesh);
    }
}

static void CreateInstances()
{
    Instance ground;
    ground.mesh = MeshQuad;
    ground.transform = Transform::Scale({ 16.0f, 1.0f, 16.0f });
    g_Instances.push_back(ground);

    Instance cube1;
    cube1.mesh = MeshCube;
    cube1.transform = Transform::Translation({ -3.2f, 1.0f, -0.6f }) * Transform::RotationY(Radians(25.0f));
    g_Instances.push_back(cube1);

    Instance cube2;
    cube2.mesh = MeshCube;
    cube2.transform = Transform::Translation({ -3.2f, 2.6f, -0.6f }) * Transform::RotationY(Radians(55.0f)) * Transform::Scale({ 0.6f, 0.6f, 0.6f });
    g_Instances.push_back(cube2);

    Instance cube3;
    cube3.mesh = MeshCube;
    cube3.transform = Transform::Translation({ 3.6f, 0.5f, -1.0f }) * Transform::RotationY(Radians(-30.0f)) * Transform::Scale({ 0.5f, 0.5f, 0.5f });
    g_Instances.push_back(cube3);

    Instance sphere1;
    sphere1.mesh = MeshSphere;
    sphere1.transform = Transform::Translation({ -1.2f, 0.75f, 2.6f }) * Transform::Scale({ 0.75f, 0.75f, 0.75f });
    g_Instances.push_back(sphere1);

    Instance sphere2;
    sphere2.mesh = MeshSphere;
    sphere2.transform = Transform::Translation({ 0.2f, 1.3f, -1.4f }) * Transform::Scale({ 1.3f, 1.3f, 1.3f });
    g_Instances.push_back(sphere2);

    Instance torus;
    torus.mesh = MeshTorus;
    torus.transform = Transform::Translation({ 2.6f, 0.32f, 1.6f }) * Transform::RotationY(Radians(20.0f));
    g_Instances.push_back(torus);

    Instance quad;
    quad.mesh = MeshQuad;
    quad.transform = Transform::Translation({ 1.8f, 1.6f, -4.2f }) * Transform::RotationY(Radians(-15.0f)) * Transform::RotationX(Radians(90.0f)) * Transform::Scale({ 1.6f, 1.0f, 1.6f });
    g_Instances.push_back(quad);
}

static void BuildBLASes()
{
    const size_t blasCount = g_Meshes.size();

    std::vector<VkAccelerationStructureGeometryKHR>             geometries(blasCount);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR>       buildRanges(blasCount);
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR>    buildInfos(blasCount);
    std::vector<VkDeviceSize>                                   scratchOffsets(blasCount);

    for (size_t i = 0; i < blasCount; i++)
    {
        const Mesh& mesh = g_Meshes[i];

        VkAccelerationStructureGeometryKHR& geometry = geometries[i];
        geometry = {};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress = mesh.vertexBuffer.address;
        geometry.geometry.triangles.vertexStride = sizeof(shared::Vertex);
        geometry.geometry.triangles.maxVertex = mesh.vertexCount - 1;
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geometry.geometry.triangles.indexData.deviceAddress = mesh.indexBuffer.address;

        buildRanges[i] = {};
        buildRanges[i].primitiveCount = mesh.indexCount / 3;

        VkAccelerationStructureBuildGeometryInfoKHR& buildInfo = buildInfos[i];
        buildInfo = {};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;
    }

    const VkDeviceSize scratchAlignment = g_VkPhysicalDevices[g_CurrentPhysicalDevice].deviceAccelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
    VkDeviceSize scratchSize = 0;

    for (size_t i = 0; i < blasCount; i++)
    {
        VkAccelerationStructureBuildSizesInfoKHR buildSizes = {};
        buildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        vkGetAccelerationStructureBuildSizesKHR(g_VkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfos[i], &buildRanges[i].primitiveCount, &buildSizes);

        AccelerationStructure& blas = g_Meshes[i].blas;
        blas = CreateAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, buildSizes.accelerationStructureSize, "BLAS");
        buildInfos[i].dstAccelerationStructure = blas.handle;

        scratchOffsets[i] = scratchSize;
        scratchSize = AlignUp(scratchSize + buildSizes.buildScratchSize, scratchAlignment);
    }

    Buffer scratch = CreateBuffer(scratchSize + scratchAlignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "BLAS Scratch");
    VkDeviceAddress scratchBase = AlignUp(scratch.address, scratchAlignment);

    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> buildRangePointers(blasCount);

    for (size_t i = 0; i < blasCount; i++)
    {
        buildInfos[i].scratchData.deviceAddress = scratchBase + scratchOffsets[i];
        buildRangePointers[i] = &buildRanges[i];
    }

    VkCommandBuffer commandBuffer = BeginOneTimeCommands();

    vkCmdBuildAccelerationStructuresKHR(commandBuffer, (uint32_t)blasCount, buildInfos.data(), buildRangePointers.data());

    VkMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

    EndOneTimeCommands(commandBuffer);

    DestroyBuffer(scratch);
}

static VkAccelerationStructureGeometryKHR GetTLASGeometry(VkDeviceAddress instanceData)
{
    VkAccelerationStructureGeometryKHR geometry = {};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instanceData;

    return geometry;
}

static VkAccelerationStructureBuildGeometryInfoKHR GetTLASBuildInfo(const VkAccelerationStructureGeometryKHR* geometry)
{
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = geometry;

    return buildInfo;
}

static void CreateTLAS()
{
    const VkDeviceSize instanceBufferSize = g_Instances.size() * sizeof(VkAccelerationStructureInstanceKHR);

    g_InstanceBuffers.resize(g_MaxFramesInFlight);

    for (Buffer& instanceBuffer : g_InstanceBuffers)
        instanceBuffer = CreateBuffer(instanceBufferSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "TLAS Instances");

    const VkAccelerationStructureGeometryKHR geometry = GetTLASGeometry(0);
    const VkAccelerationStructureBuildGeometryInfoKHR buildInfo = GetTLASBuildInfo(&geometry);

    uint32_t instanceCount = (uint32_t)g_Instances.size();

    VkAccelerationStructureBuildSizesInfoKHR buildSizes = {};
    buildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    vkGetAccelerationStructureBuildSizesKHR(g_VkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &instanceCount, &buildSizes);

    g_Tlas = CreateAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, buildSizes.accelerationStructureSize, "TLAS");

    const VkDeviceSize scratchAlignment = g_VkPhysicalDevices[g_CurrentPhysicalDevice].deviceAccelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
    g_TlasScratch = CreateBuffer(buildSizes.buildScratchSize + scratchAlignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "TLAS Scratch");
    g_TlasScratchAddress = AlignUp(g_TlasScratch.address, scratchAlignment);
}

void BuildTLAS(VkCommandBuffer commandBuffer, size_t frame)
{
    const Buffer& instanceBuffer = g_InstanceBuffers[frame];
    VkAccelerationStructureInstanceKHR* instances = (VkAccelerationStructureInstanceKHR*)instanceBuffer.mapped;

    for (uint32_t i = 0; i < g_Instances.size(); i++)
    {
        const Instance& source = g_Instances[i];

        VkAccelerationStructureInstanceKHR instance = {};
        memcpy(instance.transform.matrix, source.transform.m, sizeof(instance.transform.matrix));
        instance.instanceCustomIndex = i;
        instance.mask = 0xFF;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = g_Meshes[source.mesh].blas.address;

        instances[i] = instance;
    }

    const VkAccelerationStructureGeometryKHR geometry = GetTLASGeometry(instanceBuffer.address);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = GetTLASBuildInfo(&geometry);
    buildInfo.dstAccelerationStructure = g_Tlas.handle;
    buildInfo.scratchData.deviceAddress = g_TlasScratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR buildRange = {};
    buildRange.primitiveCount = (uint32_t)g_Instances.size();

    const VkAccelerationStructureBuildRangeInfoKHR* buildRangePointer = &buildRange;

    VkMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

    vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &buildRangePointer);

    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
}

void CreateRayTracingScene()
{
    CreateMeshes();
    CreateInstances();
    BuildBLASes();
    CreateTLAS();
}

void DestroyRayTracingScene()
{
    DestroyAccelerationStructure(g_Tlas);
    DestroyBuffer(g_TlasScratch);
    g_TlasScratchAddress = 0;

    for (Buffer& instanceBuffer : g_InstanceBuffers)
        DestroyBuffer(instanceBuffer);

    g_InstanceBuffers.clear();

    for (Mesh& mesh : g_Meshes)
    {
        DestroyAccelerationStructure(mesh.blas);
        DestroyBuffer(mesh.vertexBuffer);
        DestroyBuffer(mesh.indexBuffer);
    }

    g_Meshes.clear();
    g_Instances.clear();
}
#endif
