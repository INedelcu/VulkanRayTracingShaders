#include "RayTracingScene.h"
#include "Geometry.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

#ifdef VK_KHR_acceleration_structure
enum MeshId : uint8_t
{
    MeshQuad = 0,
    MeshCube,
    MeshSphere,
    MeshTorus,
    MeshCylinder
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

    const MeshData meshData[] = { CreateQuad(), CreateCube(), CreateUvSphere(128, 64), CreateTorus(1.0f, 0.32f, 256, 64), CreateCylinder(128) };
    const char* meshNames[] = { "Quad", "Cube", "Sphere", "Torus", "Cylinder" };

    for (size_t i = 0; i < std::size(meshData); i++)
    {
        char vertexBufferName[64];
        char indexBufferName[64];
        _snprintf_s(vertexBufferName, _TRUNCATE, "%s vertices", meshNames[i]);
        _snprintf_s(indexBufferName, _TRUNCATE, "%s indices", meshNames[i]);

        Mesh mesh;
        mesh.vertexCount = (uint32_t)meshData[i].vertices.size();
        mesh.indexCount = (uint32_t)meshData[i].indices.size();
        mesh.vertexBuffer = CreateBufferWithData(meshData[i].vertices.data(), mesh.vertexCount * sizeof(shared::Vertex), geometryUsage, vertexBufferName);
        mesh.indexBuffer = CreateBufferWithData(meshData[i].indices.data(), mesh.indexCount * sizeof(uint32_t), geometryUsage, indexBufferName);

        g_Meshes.push_back(mesh);
    }
}

static void CreateInstances()
{
    Instance ground;
    ground.mesh = MeshQuad;
    ground.transform = Transform::Scale({ 16.0f, 1.0f, 16.0f });
    ground.color = { 0.50f, 0.47f, 0.43f };
    g_Instances.push_back(ground);

    Instance cube1;
    cube1.mesh = MeshCube;
    cube1.transform = Transform::Translation({ -3.2f, 1.0f, -0.6f }) * Transform::RotationY(Radians(25.0f));
    cube1.color = { 0.50f, 0.16f, 0.10f };
    g_Instances.push_back(cube1);

    Instance cube2;
    cube2.mesh = MeshCube;
    cube2.transform = Transform::Translation({ -3.2f, 2.6f, -0.6f }) * Transform::RotationY(Radians(55.0f)) * Transform::Scale({ 0.6f, 0.6f, 0.6f });
    cube2.color = { 0.55f, 0.36f, 0.17f };
    g_Instances.push_back(cube2);

    Instance sphere0;
    sphere0.mesh = MeshSphere;
    sphere0.transform = Transform::Translation({ 3.6f, 1.0f, -1.0f });
    sphere0.color = { 1.0f, 1.0f, 1.0f };
    sphere0.material = MaterialType::Metal;
    sphere0.roughness = 0.15f;
    g_Instances.push_back(sphere0);

    Instance sphere1;
    sphere1.mesh = MeshSphere;
    sphere1.transform = Transform::Translation({ -1.2f, 0.75f, 2.6f }) * Transform::Scale({ 0.75f, 0.75f, 0.75f });
    sphere1.color = { 0.0f, 0.3f, 0.0f };
    sphere1.roughness = 0.0f;
    g_Instances.push_back(sphere1);

    Instance sphere2;
    sphere2.mesh = MeshSphere;
    sphere2.transform = Transform::Translation({ 0.2f, 1.3f, -1.4f }) * Transform::Scale({ 1.3f, 1.3f, 1.3f });
    sphere2.material = MaterialType::Glass;
    sphere2.color = { 0.92f, 0.86f, 0.19f };
    sphere2.ior = 1.7f;
    sphere2.roughness = 0.25f;
    g_Instances.push_back(sphere2);

    Instance torus;
    torus.mesh = MeshTorus;
    torus.transform = Transform::Translation({ 2.6f, 0.32f, 1.6f }) * Transform::RotationY(Radians(20.0f));
    torus.color = { 0.80f, 0.55f, 0.15f };
    g_Instances.push_back(torus);

    Instance quad;
    quad.mesh = MeshQuad;
    quad.transform = Transform::Translation({ 1.8f, 1.6f, -4.2f }) * Transform::RotationY(Radians(-15.0f)) * Transform::RotationX(Radians(90.0f)) * Transform::Scale({ 1.6f, 1.0f, 1.6f });
    quad.color = { 0.20f, 0.50f, 0.22f };
    g_Instances.push_back(quad);

    Instance sphere3;
    sphere3.mesh = MeshSphere;
    sphere3.transform = Transform::Translation({ 0.35f, 0.55f, 2.95f }) * Transform::Scale({ 0.55f, 0.55f, 0.55f });
    sphere3.material = MaterialType::Glass;
    sphere3.color = { 0.98f, 0.98f, 0.98f };
    sphere3.ior = 1.5f;
    sphere3.roughness = 0.0f;
    g_Instances.push_back(sphere3);

    Instance cube4;
    cube4.mesh = MeshCube;
    cube4.transform = Transform::Translation({ -3.3f, 0.45f, 2.3f }) * Transform::RotationY(Radians(-20.0f)) * Transform::Scale({ 0.45f, 0.45f, 0.45f });
    cube4.material = MaterialType::Glass;
    cube4.color = { 0.55f, 0.75f, 0.95f };
    cube4.ior = 1.45f;
    cube4.extinction = 1.2f;
    cube4.roughness = 0.35f;
    g_Instances.push_back(cube4);

    Instance torus2;
    torus2.mesh = MeshTorus;
    torus2.transform = Transform::Translation({ -2.0f, 0.144f, 4.3f }) * Transform::RotationY(Radians(-35.0f)) * Transform::Scale({ 0.45f, 0.45f, 0.45f });
    torus2.material = MaterialType::Glass;
    torus2.color = { 0.85f, 0.15f, 0.2f };
    torus2.ior = 1.55f;
    torus2.extinction = 3.0f;
    torus2.roughness = 0.0f;
    g_Instances.push_back(torus2);

    Instance sphere4;
    sphere4.mesh = MeshSphere;
    sphere4.transform = Transform::Translation({ 4.4f, 0.6f, 2.7f }) * Transform::Scale({ 0.6f, 0.6f, 0.6f });
    sphere4.color = { 0.05f, 0.15f, 0.55f };
    sphere4.ior = 1.974f;
    sphere4.roughness = 0.15f;
    g_Instances.push_back(sphere4);

    Instance sphere5;
    sphere5.mesh = MeshSphere;
    sphere5.transform = Transform::Translation({ 2.0f, 0.5f, -0.8f }) * Transform::Scale({ 0.5f, 0.5f, 0.5f });
    sphere5.color = { 0.45f, 0.3f, 0.6f };
    sphere5.roughness = 0.4f;
    g_Instances.push_back(sphere5);

    Instance cube5;
    cube5.mesh = MeshCube;
    cube5.transform = Transform::Translation({ 5.2f, 1.0f, -2.4f }) * Transform::RotationY(Radians(15.0f)) * Transform::Scale({ 0.35f, 1.0f, 0.35f });
    cube5.material = MaterialType::Glass;
    cube5.color = { 0.3f, 0.7f, 0.35f };
    cube5.ior = 1.52f;
    cube5.extinction = 2.0f;
    cube5.roughness = 0.05f;
    g_Instances.push_back(cube5);

    Instance cylinder1;
    cylinder1.mesh = MeshCylinder;
    cylinder1.transform = Transform::Translation({ -1.7f, 1.2f, -3.6f }) * Transform::Scale({ 0.4f, 1.2f, 0.4f });
    cylinder1.color = { 0.9f, 0.9f, 0.87f };
    cylinder1.roughness = 0.2f;
    g_Instances.push_back(cylinder1);

    Instance cylinder2;
    cylinder2.mesh = MeshCylinder;
    cylinder2.transform = Transform::Translation({ 3.2f, 0.12f, 4.0f }) * Transform::Scale({ 0.6f, 0.12f, 0.6f });
    cylinder2.color = { 0.03f, 0.03f, 0.035f };
    cylinder2.roughness = 0.1f;
    g_Instances.push_back(cylinder2);

    // Stands on cylinder2, lifted a hair above its top so the two faces don't coincide.
    Instance cylinder3;
    cylinder3.mesh = MeshCylinder;
    cylinder3.transform = Transform::Translation({ 3.2f, 0.645f, 4.0f }) * Transform::Scale({ 0.3f, 0.4f, 0.3f });
    cylinder3.material = MaterialType::Glass;
    cylinder3.color = { 0.95f, 0.6f, 0.15f };
    cylinder3.ior = 1.5f;
    cylinder3.extinction = 1.5f;
    cylinder3.roughness = 0.0f;
    g_Instances.push_back(cylinder3);

    Instance torus3;
    torus3.mesh = MeshTorus;
    torus3.transform = Transform::Translation({ -4.8f, 0.73f, 1.0f }) * Transform::RotationY(Radians(60.0f)) * Transform::RotationX(Radians(90.0f)) * Transform::Scale({ 0.55f, 0.55f, 0.55f });
    torus3.color = { 0.6f, 0.04f, 0.04f };
    torus3.ior = 1.6f;
    torus3.roughness = 0.05f;
    g_Instances.push_back(torus3);

    Instance cylinder4;
    cylinder4.mesh = MeshCylinder;
    cylinder4.transform = Transform::Translation({ -0.4f, 0.302f, 4.5f }) * Transform::RotationY(Radians(30.0f)) * Transform::RotationX(Radians(90.0f)) * Transform::Scale({ 0.3f, 0.6f, 0.3f });
    cylinder4.color = { 0.85f, 0.35f, 0.08f };
    g_Instances.push_back(cylinder4);

    Instance sphere6;
    sphere6.mesh = MeshSphere;
    sphere6.transform = Transform::Translation({ 1.6f, 0.45f, 4.8f }) * Transform::Scale({ 0.45f, 0.45f, 0.45f });
    sphere6.material = MaterialType::Metal;
    sphere6.color = { 1.0f, 0.766f, 0.336f };
    sphere6.roughness = 0.1f;
    g_Instances.push_back(sphere6);

    Instance cube6;
    cube6.mesh = MeshCube;
    cube6.transform = Transform::Translation({ -3.9f, 0.4f, 3.6f }) * Transform::RotationY(Radians(45.0f)) * Transform::Scale({ 0.4f, 0.4f, 0.4f });
    cube6.material = MaterialType::Metal;
    cube6.color = { 0.562f, 0.565f, 0.578f };
    cube6.roughness = 0.0f;
    g_Instances.push_back(cube6);

    Instance torus4;
    torus4.mesh = MeshTorus;
    torus4.transform = Transform::Translation({ 7.5f, 0.795f, -3.5f }) * Transform::RotationY(Radians(-50.0f)) * Transform::RotationX(Radians(90.0f)) * Transform::Scale({ 0.6f, 0.6f, 0.6f });
    torus4.material = MaterialType::Metal;
    torus4.color = { 0.955f, 0.638f, 0.538f };
    torus4.roughness = 0.3f;
    g_Instances.push_back(torus4);

    Instance cube7;
    cube7.mesh = MeshCube;
    cube7.transform = Transform::Translation({ 9.0f, 0.6f, -2.5f }) * Transform::RotationY(Radians(30.0f)) * Transform::Scale({ 0.6f, 0.6f, 0.6f });
    cube7.material = MaterialType::Metal;
    cube7.color = { 0.549f, 0.556f, 0.554f };
    cube7.roughness = 0.0f;
    g_Instances.push_back(cube7);

    Instance sphere7;
    sphere7.mesh = MeshSphere;
    sphere7.transform = Transform::Translation({ -10.5f, 0.8f, -4.5f }) * Transform::Scale({ 0.8f, 0.8f, 0.8f });
    sphere7.material = MaterialType::Metal;
    sphere7.color = { 0.972f, 0.960f, 0.915f };
    sphere7.roughness = 0.2f;
    g_Instances.push_back(sphere7);
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
        // Opaque unless an instance overrides it, as glass does for shadow rays. Its any-hit shader multiplies the
        // shadow ray's transmittance per crossing, so it must run once per triangle.
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR | VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
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
        instance.instanceShaderBindingTableRecordOffset = i * shared::RAY_TYPE_COUNT;
        instance.mask = 0xFF;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

        // Non-opaque so shadow rays run the glass any-hit shader; radiance rays force everything opaque.
        if (source.material == MaterialType::Glass)
            instance.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;

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
