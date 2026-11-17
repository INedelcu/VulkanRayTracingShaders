#include "RayTracingScene.h"
#include "Geometry.h"
#include "Textures.h"

#include <cstdio>
#include <cstring>
#include <deque>
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

// A mesh and the acceleration structure built over it, which the instances of the mesh refer to.
struct Mesh
{
    Buffer                vertexBuffer;
    Buffer                indexBuffer;
    uint32_t              vertexCount = 0;
    uint32_t              indexCount  = 0;
    AccelerationStructure blas;
};

// An instance of a mesh, with the material whose surface shader its hits run. Several instances can share one
// material; without a material, or with one that has no shader, an instance stops the rays that hit it and nothing
// else. This is what the TLAS is built from and what the hit records are made of.
struct InstanceDescription
{
    uint32_t                  mesh = 0;           // index into g_Meshes
    Transform                 transform;
    const RayTracingMaterial* material = nullptr; // one of g_Materials
};

static std::vector<Mesh>                g_Meshes;
static std::deque<RayTracingMaterial>   g_Materials; // a deque so that adding one leaves the instances' pointers into it valid
static std::vector<InstanceDescription> g_Instances;
static std::vector<Buffer>              g_InstanceBuffers; // one per frame in flight, host visible so the CPU can rewrite it while older frames are still building
static Buffer                           g_TlasScratch;
static VkDeviceAddress                  g_TlasScratchAddress = 0;
AccelerationStructure                   g_Tlas;

static RayTracingSurfaceShader g_DiffuseShader;
static RayTracingSurfaceShader g_MetalShader;
static RayTracingSurfaceShader g_GlassShader;

static std::vector<Image> g_Textures;
static uint32_t           g_WhiteTextureSlot = UINT32_MAX;   // the texture of a material that has none of its own
static uint32_t           g_CheckerTextureSlot = UINT32_MAX;
static uint32_t           g_TerrainTextureSlot = UINT32_MAX;

// Keeps the texture with the scene and returns the slot of its descriptor in the resource heap, which is what a
// material's texture property holds. UINT32_MAX when the texture is empty or the heap is full.
static uint32_t AddTexture(DescriptorHeap& resourceHeap, const Image& texture)
{
    Image owned = texture;

    if (owned.image == VK_NULL_HANDLE)
        return UINT32_MAX;

    const uint32_t slot = resourceHeap.Allocate();

    if (slot == UINT32_MAX || !resourceHeap.WriteSampledImage(slot, owned))
    {
        DestroyImage(owned);
        return UINT32_MAX;
    }

    g_Textures.push_back(owned);

    return slot;
}

// The textures the materials sample. A file that won't load leaves its materials white rather than stopping the
// scene, so a missing or broken .png shows up as a message and an untextured surface.
static bool CreateTextures(DescriptorHeap& resourceHeap)
{
    g_WhiteTextureSlot = AddTexture(resourceHeap, CreateWhiteTexture());
    g_CheckerTextureSlot = AddTexture(resourceHeap, LoadTexture("Checker.png"));
    g_TerrainTextureSlot = AddTexture(resourceHeap, LoadTexture("Terrain.png"));

    if (g_CheckerTextureSlot == UINT32_MAX)
        g_CheckerTextureSlot = g_WhiteTextureSlot;

    if (g_TerrainTextureSlot == UINT32_MAX)
        g_TerrainTextureSlot = g_WhiteTextureSlot;

    return g_WhiteTextureSlot != UINT32_MAX;
}

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

// Radiance rays run a surface's closest-hit shader. Shadow rays only need to know whether a surface stops them, which
// takes no shader; glass filters them in an any-hit shader instead.
static bool CreateSurfaceShaders(const RayTracingGlobalResources& globals)
{
    RayTracingHitGroup diffuse[shared::RAY_TYPE_COUNT];
    diffuse[shared::RAY_TYPE_RADIANCE].closestHit = "ClosestHitDiffuse";

    RayTracingHitGroup metal[shared::RAY_TYPE_COUNT];
    metal[shared::RAY_TYPE_RADIANCE].closestHit = "ClosestHitMetal";

    RayTracingHitGroup glass[shared::RAY_TYPE_COUNT];
    glass[shared::RAY_TYPE_RADIANCE].closestHit = "ClosestHitGlass";
    glass[shared::RAY_TYPE_SHADOW].anyHit = "AnyHitGlassShadow";

    if (!g_DiffuseShader.Create("MaterialDiffuse", diffuse, globals) || !g_MetalShader.Create("MaterialMetal", metal, globals) || !g_GlassShader.Create("MaterialGlass", glass, globals))
        return false;

    // What a material of each shader is before it sets anything: a white matte surface, and a dielectric coat or body
    // of the index of glass. A property a material doesn't set and a shader has no default for is left zero, which
    // ior can't be: a coat of index 0 reflects everything.
    g_DiffuseShader.DefaultProperties().SetVector("color", { 1.0f, 1.0f, 1.0f });
    g_DiffuseShader.DefaultProperties().SetFloat("ior", 1.5f);
    g_DiffuseShader.DefaultProperties().SetFloat("roughness", 1.0f);

    // White by default, so a material with no texture of its own still has one to sample and the shader needs no
    // branch. An unset texture property would hold slot 0, which is a render target, not a texture at all.
    g_DiffuseShader.DefaultProperties().SetTexture("BaseColorMap", g_WhiteTextureSlot);
    g_DiffuseShader.DefaultProperties().SetVector("uvScale", { 1.0f, 1.0f });

    g_MetalShader.DefaultProperties().SetVector("color", { 1.0f, 1.0f, 1.0f });
    g_MetalShader.DefaultProperties().SetFloat("roughness", 1.0f);

    g_GlassShader.DefaultProperties().SetVector("color", { 1.0f, 1.0f, 1.0f });
    g_GlassShader.DefaultProperties().SetFloat("ior", 1.5f);
    g_GlassShader.DefaultProperties().SetFloat("extinction", 1.0f);
    g_GlassShader.DefaultProperties().SetFloat("roughness", 1.0f);

    return true;
}

// Adds a material of the shader, holding the values that shader gives a material of its own. Returns it to be filled
// in, e.g. AddMaterial(g_GlassShader).SetFloat("ior", 1.7f), and to be pointed at by the instances that use it.
static RayTracingMaterial& AddMaterial(const RayTracingSurfaceShader& shader)
{
    return g_Materials.emplace_back(&shader);
}

static void AddInstance(const InstanceDescription& description)
{
    g_Instances.push_back(description);
}

// Each instance sets only the properties that differ from the defaults its shader gives a material: a diffuse or a
// glass surface is white, with a coat of index 1.5 and a matte base, unless it says otherwise.
static void CreateInstances()
{
    InstanceDescription ground;
    ground.mesh = MeshQuad;
    ground.transform = Transform::Scale({ 16.0f, 1.0f, 16.0f });
    ground.material = &AddMaterial(g_DiffuseShader)
        .SetTexture("BaseColorMap", g_TerrainTextureSlot)
        .SetVector("uvScale", { 16.0f, 16.0f });
    AddInstance(ground);

    InstanceDescription cube1;
    cube1.mesh = MeshCube;
    cube1.transform = Transform::Translation({ -3.2f, 1.0f, -0.6f }) * Transform::RotationY(Radians(25.0f));
    cube1.material = &AddMaterial(g_DiffuseShader)
        .SetVector("color", { 1.0f, 1.0f, 1.0f })
        .SetTexture("BaseColorMap", g_CheckerTextureSlot);
    AddInstance(cube1);

    InstanceDescription cube2;
    cube2.mesh = MeshCube;
    cube2.transform = Transform::Translation({ -3.2f, 2.6f, -0.6f }) * Transform::RotationY(Radians(55.0f)) * Transform::Scale({ 0.6f, 0.6f, 0.6f });
    cube2.material = &AddMaterial(g_DiffuseShader).SetVector("color", { 0.55f, 0.36f, 0.17f });
    AddInstance(cube2);

    InstanceDescription sphere0;
    sphere0.mesh = MeshSphere;
    sphere0.transform = Transform::Translation({ 3.6f, 1.0f, -1.0f });
    sphere0.material = &AddMaterial(g_MetalShader).SetFloat("roughness", 0.15f);
    AddInstance(sphere0);

    InstanceDescription sphere1;
    sphere1.mesh = MeshSphere;
    sphere1.transform = Transform::Translation({ -1.2f, 0.75f, 2.6f }) * Transform::Scale({ 0.75f, 0.75f, 0.75f });
    sphere1.material = &AddMaterial(g_DiffuseShader)
        .SetVector("color", { 0.0f, 0.3f, 0.0f })
        .SetFloat("roughness", 0.0f);
    AddInstance(sphere1);

    InstanceDescription sphere2;
    sphere2.mesh = MeshSphere;
    sphere2.transform = Transform::Translation({ 0.2f, 1.3f, -1.4f }) * Transform::Scale({ 1.3f, 1.3f, 1.3f });
    sphere2.material = &AddMaterial(g_GlassShader)
        .SetVector("color", { 0.92f, 0.86f, 0.19f })
        .SetFloat("ior", 1.7f)
        .SetFloat("roughness", 0.25f);
    AddInstance(sphere2);

    InstanceDescription torus;
    torus.mesh = MeshTorus;
    torus.transform = Transform::Translation({ 2.6f, 0.32f, 1.6f }) * Transform::RotationY(Radians(20.0f));
    torus.material = &AddMaterial(g_DiffuseShader).SetVector("color", { 0.80f, 0.55f, 0.15f });
    AddInstance(torus);

    InstanceDescription quad;
    quad.mesh = MeshQuad;
    quad.transform = Transform::Translation({ 1.8f, 1.6f, -4.2f }) * Transform::RotationY(Radians(-15.0f)) * Transform::RotationX(Radians(90.0f)) * Transform::Scale({ 1.6f, 1.0f, 1.6f });
    quad.material = &AddMaterial(g_DiffuseShader).SetVector("color", { 0.20f, 0.50f, 0.22f });
    AddInstance(quad);

    InstanceDescription sphere3;
    sphere3.mesh = MeshSphere;
    sphere3.transform = Transform::Translation({ 0.35f, 0.55f, 2.95f }) * Transform::Scale({ 0.55f, 0.55f, 0.55f });
    sphere3.material = &AddMaterial(g_GlassShader)
        .SetVector("color", { 0.98f, 0.98f, 0.98f })
        .SetFloat("roughness", 0.0f);
    AddInstance(sphere3);

    InstanceDescription cube4;
    cube4.mesh = MeshCube;
    cube4.transform = Transform::Translation({ -3.3f, 0.45f, 2.3f }) * Transform::RotationY(Radians(-20.0f)) * Transform::Scale({ 0.45f, 0.45f, 0.45f });
    cube4.material = &AddMaterial(g_GlassShader)
        .SetVector("color", { 0.55f, 0.75f, 0.95f })
        .SetFloat("ior", 1.45f)
        .SetFloat("extinction", 1.2f)
        .SetFloat("roughness", 0.35f);
    AddInstance(cube4);

    InstanceDescription torus2;
    torus2.mesh = MeshTorus;
    torus2.transform = Transform::Translation({ -2.0f, 0.144f, 4.3f }) * Transform::RotationY(Radians(-35.0f)) * Transform::Scale({ 0.45f, 0.45f, 0.45f });
    torus2.material = &AddMaterial(g_GlassShader)
        .SetVector("color", { 0.85f, 0.15f, 0.2f })
        .SetFloat("ior", 1.55f)
        .SetFloat("extinction", 3.0f)
        .SetFloat("roughness", 0.0f);
    AddInstance(torus2);

    InstanceDescription sphere4;
    sphere4.mesh = MeshSphere;
    sphere4.transform = Transform::Translation({ 4.4f, 0.6f, 2.7f }) * Transform::Scale({ 0.6f, 0.6f, 0.6f });
    sphere4.material = &AddMaterial(g_DiffuseShader)
        .SetVector("color", { 0.05f, 0.15f, 0.55f })
        .SetFloat("ior", 1.974f)
        .SetFloat("roughness", 0.15f);
    AddInstance(sphere4);

    InstanceDescription sphere5;
    sphere5.mesh = MeshSphere;
    sphere5.transform = Transform::Translation({ 2.0f, 0.5f, -0.8f }) * Transform::Scale({ 0.5f, 0.5f, 0.5f });
    sphere5.material = &AddMaterial(g_DiffuseShader)
        .SetVector("color", { 0.45f, 0.3f, 0.6f })
        .SetFloat("roughness", 0.4f);
    AddInstance(sphere5);

    InstanceDescription cube5;
    cube5.mesh = MeshCube;
    cube5.transform = Transform::Translation({ 5.2f, 1.0f, -2.4f }) * Transform::RotationY(Radians(15.0f)) * Transform::Scale({ 0.35f, 1.0f, 0.35f });
    cube5.material = &AddMaterial(g_GlassShader)
        .SetVector("color", { 0.3f, 0.7f, 0.35f })
        .SetFloat("ior", 1.52f)
        .SetFloat("extinction", 2.0f)
        .SetFloat("roughness", 0.05f);
    AddInstance(cube5);

    InstanceDescription cylinder1;
    cylinder1.mesh = MeshCylinder;
    cylinder1.transform = Transform::Translation({ -1.7f, 1.2f, -3.6f }) * Transform::Scale({ 0.4f, 1.2f, 0.4f });
    cylinder1.material = &AddMaterial(g_DiffuseShader)
        .SetVector("color", { 0.9f, 0.9f, 0.87f })
        .SetFloat("roughness", 0.2f);
    AddInstance(cylinder1);

    InstanceDescription cylinder2;
    cylinder2.mesh = MeshCylinder;
    cylinder2.transform = Transform::Translation({ 3.2f, 0.12f, 4.0f }) * Transform::Scale({ 0.6f, 0.12f, 0.6f });
    cylinder2.material = &AddMaterial(g_DiffuseShader)
        .SetVector("color", { 0.03f, 0.03f, 0.035f })
        .SetFloat("roughness", 0.1f);
    AddInstance(cylinder2);

    // Stands on cylinder2, lifted a hair above its top so the two faces don't coincide.
    InstanceDescription cylinder3;
    cylinder3.mesh = MeshCylinder;
    cylinder3.transform = Transform::Translation({ 3.2f, 0.645f, 4.0f }) * Transform::Scale({ 0.3f, 0.4f, 0.3f });
    cylinder3.material = &AddMaterial(g_GlassShader)
        .SetVector("color", { 0.95f, 0.6f, 0.15f })
        .SetFloat("extinction", 1.5f)
        .SetFloat("roughness", 0.0f);
    AddInstance(cylinder3);

    InstanceDescription torus3;
    torus3.mesh = MeshTorus;
    torus3.transform = Transform::Translation({ -4.8f, 0.73f, 1.0f }) * Transform::RotationY(Radians(60.0f)) * Transform::RotationX(Radians(90.0f)) * Transform::Scale({ 0.55f, 0.55f, 0.55f });
    torus3.material = &AddMaterial(g_DiffuseShader)
        .SetVector("color", { 0.6f, 0.04f, 0.04f })
        .SetFloat("ior", 1.6f)
        .SetFloat("roughness", 0.05f);
    AddInstance(torus3);

    InstanceDescription cylinder4;
    cylinder4.mesh = MeshCylinder;
    cylinder4.transform = Transform::Translation({ -0.4f, 0.302f, 4.5f }) * Transform::RotationY(Radians(30.0f)) * Transform::RotationX(Radians(90.0f)) * Transform::Scale({ 0.3f, 0.6f, 0.3f });
    cylinder4.material = &AddMaterial(g_DiffuseShader).SetVector("color", { 0.85f, 0.35f, 0.08f });
    AddInstance(cylinder4);

    InstanceDescription sphere6;
    sphere6.mesh = MeshSphere;
    sphere6.transform = Transform::Translation({ 1.6f, 0.45f, 4.8f }) * Transform::Scale({ 0.45f, 0.45f, 0.45f });
    sphere6.material = &AddMaterial(g_MetalShader)
        .SetVector("color", { 1.0f, 0.766f, 0.336f })
        .SetFloat("roughness", 0.1f);
    AddInstance(sphere6);

    InstanceDescription cube6;
    cube6.mesh = MeshCube;
    cube6.transform = Transform::Translation({ -3.9f, 0.4f, 3.6f }) * Transform::RotationY(Radians(45.0f)) * Transform::Scale({ 0.4f, 0.4f, 0.4f });
    cube6.material = &AddMaterial(g_MetalShader)
        .SetVector("color", { 0.562f, 0.565f, 0.578f })
        .SetFloat("roughness", 0.0f);
    AddInstance(cube6);

    InstanceDescription torus4;
    torus4.mesh = MeshTorus;
    torus4.transform = Transform::Translation({ 7.5f, 0.795f, -3.5f }) * Transform::RotationY(Radians(-50.0f)) * Transform::RotationX(Radians(90.0f)) * Transform::Scale({ 0.6f, 0.6f, 0.6f });
    torus4.material = &AddMaterial(g_MetalShader)
        .SetVector("color", { 0.955f, 0.638f, 0.538f })
        .SetFloat("roughness", 0.3f);
    AddInstance(torus4);

    InstanceDescription cube7;
    cube7.mesh = MeshCube;
    cube7.transform = Transform::Translation({ 9.0f, 0.6f, -2.5f }) * Transform::RotationY(Radians(30.0f)) * Transform::Scale({ 0.6f, 0.6f, 0.6f });
    cube7.material = &AddMaterial(g_MetalShader)
        .SetVector("color", { 0.549f, 0.556f, 0.554f })
        .SetFloat("roughness", 0.0f);
    AddInstance(cube7);

    InstanceDescription sphere7;
    sphere7.mesh = MeshSphere;
    sphere7.transform = Transform::Translation({ -10.5f, 0.8f, -4.5f }) * Transform::Scale({ 0.8f, 0.8f, 0.8f });
    sphere7.material = &AddMaterial(g_MetalShader)
        .SetVector("color", { 0.972f, 0.960f, 0.915f })
        .SetFloat("roughness", 0.2f);
    AddInstance(sphere7);
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
        const InstanceDescription& source = g_Instances[i];

        VkAccelerationStructureInstanceKHR instance = {};
        memcpy(instance.transform.matrix, source.transform.m, sizeof(instance.transform.matrix));
        instance.instanceCustomIndex = i;
        instance.instanceShaderBindingTableRecordOffset = i * shared::RAY_TYPE_COUNT;
        instance.mask = 0xFF;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

        // Non-opaque so rays run the surface's any-hit shader, as shadow rays do for glass; radiance rays force
        // everything opaque.
        const RayTracingSurfaceShader* shader = source.material != nullptr ? source.material->Shader() : nullptr;

        if (shader != nullptr && !shader->IsOpaque())
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

// The hit record of each instance: its material's surface shader and local data, with the buffers of its mesh.
void GetHitRecords(RayTracingHitRecords& hitRecords)
{
    hitRecords.Clear();

    std::vector<uint8_t> localData;
    ShaderProperties     meshProperties; // the one thing in a record the material doesn't decide: the instance's mesh

    for (const InstanceDescription& instance : g_Instances)
    {
        if (instance.material == nullptr)
        {
            hitRecords.Add(nullptr, nullptr);
            continue;
        }

        const Mesh& mesh = g_Meshes[instance.mesh];

        meshProperties.SetBuffer("Vertices", mesh.vertexBuffer.address);
        meshProperties.SetBuffer("Indices", mesh.indexBuffer.address);

        instance.material->WriteLocalData(localData, &meshProperties);

        hitRecords.Add(instance.material->Shader(), localData.data());
    }
}

bool CreateRayTracingScene(const RayTracingGlobalResources& globals, DescriptorHeap& resourceHeap)
{
    // Before the shaders: the white texture is one of the defaults a material of a shader starts with.
    if (!CreateTextures(resourceHeap) || !CreateSurfaceShaders(globals))
        return false;

    CreateMeshes();
    CreateInstances();
    BuildBLASes();
    CreateTLAS();

    return true;
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

    for (Image& texture : g_Textures)
        DestroyImage(texture);

    g_Textures.clear();
    g_WhiteTextureSlot = UINT32_MAX;
    g_CheckerTextureSlot = UINT32_MAX;

    g_Meshes.clear();
    g_Instances.clear();
    g_Materials.clear();

    g_DiffuseShader.Destroy();
    g_MetalShader.Destroy();
    g_GlassShader.Destroy();
}
#endif
