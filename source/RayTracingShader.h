#pragma once

#include "ShaderProperties.h"
#include "ShaderReflection.h"
#include "SharedTypes.h"
#include "VulkanDevice.h"

#include <string>
#include <vector>

// Where the resources every shader shares are bound, by the name the shaders declare them with (the global root
// signature in DXR). Shaders give these a register in a space other than 0, e.g. register(u1, space1).
class RayTracingGlobalResources
{
public:
    enum class Source
    {
        PushData,    // a constant buffer whose contents are in the push data
        PushAddress, // a buffer or acceleration structure whose address is in the push data
        Heap,        // a descriptor in the resource heap
    };

    struct Resource
    {
        std::string name;
        Source      source = Source::PushData;
        uint32_t    offset = 0; // bytes into the push data or the heap
        uint32_t    size   = 0; // bytes of push data, 0 for the other sources
    };

    void AddPushData(const char* name, uint32_t pushOffset, uint32_t size);
    void AddPushAddress(const char* name, uint32_t pushOffset);
    void AddHeapResource(const char* name, VkDeviceSize heapOffset);

    // The bytes one descriptor takes in the resource heap, DescriptorHeap::SlotSize(). A texture property holds the
    // slot its descriptor is in, which this turns into the offset of that descriptor. Local textures need it.
    void     SetHeapSlotSize(VkDeviceSize slotSize) { m_HeapSlotSize = (uint32_t)slotSize; }
    uint32_t HeapSlotSize() const { return m_HeapSlotSize; }

    const Resource* Find(const std::string& name) const;

private:
    std::vector<Resource> m_Resources;
    uint32_t              m_HeapSlotSize = 0;
};

// A value a shader reads from its own shader binding table record (the local root signature in DXR), found by the
// name a material sets it under: a member of a local constant buffer, or a local buffer or texture, bound by what the
// record holds. Shaders declare local resources without a register, which puts them in space 0.
struct RayTracingShaderProperty
{
    std::string        name;
    ShaderPropertyType type       = ShaderPropertyType::Unknown;
    uint32_t           offset     = 0; // bytes into the record's local data
    uint32_t           size       = 0; // bytes the value takes there
    uint32_t           components = 0; // 1 for a scalar, 2 to 4 for a vector, 1 for a texture or a buffer
};

// A shader module and where each of its resources is bound: global resources as RayTracingGlobalResources says,
// local ones packed in declaration order into the local data that follows the shader handle in its records.
class RayTracingShader
{
public:
    void Destroy();

    const std::string& Name() const { return m_Name; }
    VkShaderModule     Module() const { return m_Module; }

    const std::vector<VkDescriptorSetAndBindingMappingEXT>& Mappings() const { return m_Mappings; }
    const std::vector<RayTracingShaderProperty>&            Properties() const { return m_Properties; }

    uint32_t LocalDataSize() const { return m_LocalDataSize; }

    // The values a material of this shader starts with, the ones it doesn't set itself. Fill these in after Create,
    // e.g. DefaultProperties().SetFloat("ior", 1.5f); a property no block sets is left zero.
    ShaderProperties&       DefaultProperties() { return m_DefaultProperties; }
    const ShaderProperties& DefaultProperties() const { return m_DefaultProperties; }

    // Writes the values a block holds into local data laid out for this shader, LocalDataSize() bytes of it, leaving
    // the rest of it alone. A name this shader has no property for is skipped, since DXC removes the resources a
    // module never uses. False when it has one of a type the value can't be written as.
    bool WriteProperties(uint8_t* localData, const ShaderProperties& properties) const;

protected:
    bool Load(const char* name, const RayTracingGlobalResources& globals);
    bool HasEntryPoint(const std::string& entryPoint, VkShaderStageFlagBits stage) const;

private:
    bool AddLocalResource(const ShaderResourceBinding& binding, const RayTracingGlobalResources& globals);

    const RayTracingShaderProperty* FindProperty(const std::string& name) const;

    std::string      m_Name;
    VkShaderModule   m_Module = VK_NULL_HANDLE;
    ShaderReflection m_Reflection;
    uint32_t         m_LocalDataSize = 0;
    ShaderProperties m_DefaultProperties;

    std::vector<VkDescriptorSetAndBindingMappingEXT> m_Mappings;
    std::vector<RayTracingShaderProperty>            m_Properties;
};

// The entry point of ray tracing: the ray generation shader and, as it decides which ray types there are, the miss
// shader of each, all in one module. Its local data goes in the ray generation and miss records.
class RayTracingRayGenShader : public RayTracingShader
{
public:
    // missEntryPoints holds one miss shader per ray type.
    bool Create(const char* name, const char* rayGenEntryPoint, const char* const missEntryPoints[shared::RAY_TYPE_COUNT], const RayTracingGlobalResources& globals);

    const std::string& RayGenEntryPoint() const { return m_RayGenEntryPoint; }
    const std::string& MissEntryPoint(uint32_t rayType) const { return m_MissEntryPoints[rayType]; }

    // The values of this shader's own local resources, which replace the ones its records held so far.
    bool SetProperties(const ShaderProperties& properties);

    const std::vector<uint8_t>& LocalData() const { return m_LocalData; }

private:
    std::string          m_RayGenEntryPoint;
    std::string          m_MissEntryPoints[shared::RAY_TYPE_COUNT];
    std::vector<uint8_t> m_LocalData;
};

// The shaders that run when a ray of one type hits a surface. Either can be empty; with both empty the hit does
// nothing but stop the ray.
struct RayTracingHitGroup
{
    std::string closestHit;
    std::string anyHit;
};

// What a kind of surface does with the rays that hit it: one hit group per ray type, all in one module.
class RayTracingSurfaceShader : public RayTracingShader
{
public:
    // hitGroups holds one hit group per ray type.
    bool Create(const char* name, const RayTracingHitGroup hitGroups[shared::RAY_TYPE_COUNT], const RayTracingGlobalResources& globals);

    const RayTracingHitGroup& HitGroup(uint32_t rayType) const { return m_HitGroups[rayType]; }

    // False when some ray type has an any-hit shader: instances of the surface must then be non-opaque for it to run.
    bool IsOpaque() const;

private:
    RayTracingHitGroup m_HitGroups[shared::RAY_TYPE_COUNT];
};
