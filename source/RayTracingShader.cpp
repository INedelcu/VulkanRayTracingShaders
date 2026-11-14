#include "RayTracingShader.h"
#include "Shaders.h"
#include "VulkanBuffers.h"

#include <spirv-headers/spirv.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstring>

static const uint32_t kLocalResourceSet = 0;

static void ReportError(const char* format, ...)
{
    char message[512];

    va_list arguments;
    va_start(arguments, format);
    _vsnprintf_s(message, _TRUNCATE, format, arguments);
    va_end(arguments);

    OutputDebugString(message);
    OutputDebugString("\n");
}

static VkSpirvResourceTypeFlagsEXT ResourceMask(ShaderResourceType type)
{
    switch (type)
    {
    case ShaderResourceType::ConstantBuffer:        return VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT;
    // Read-write too because of https://github.com/microsoft/DirectXShaderCompiler/issues/8492
    case ShaderResourceType::StorageBuffer:         return VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT | VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT;
    case ShaderResourceType::SampledImage:          return VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT;
    case ShaderResourceType::StorageImage:          return VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT;
    case ShaderResourceType::Sampler:               return VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT;
    case ShaderResourceType::AccelerationStructure: return VK_SPIRV_RESOURCE_TYPE_ACCELERATION_STRUCTURE_BIT_EXT;
    default:                                        return 0;
    }
}

// How a property's type reads in a message.
static const char* PropertyTypeName(ShaderPropertyType type, uint32_t components)
{
    switch (type)
    {
    case ShaderPropertyType::Float:   return components == 1 ? "a float" : components == 2 ? "a float2" : components == 3 ? "a float3" : components == 4 ? "a float4" : "a float matrix";
    case ShaderPropertyType::Int:     return components == 1 ? "an int" : "an int vector";
    case ShaderPropertyType::Uint:    return components == 1 ? "a uint" : "a uint vector";
    case ShaderPropertyType::Texture: return "a texture";
    case ShaderPropertyType::Buffer:  return "a buffer";
    default:                          return "of a type no property holds";
    }
}

// Buffers and acceleration structures are bound by their address; images and samplers need a descriptor.
static bool HasAddress(ShaderResourceType type)
{
    return type == ShaderResourceType::ConstantBuffer || type == ShaderResourceType::StorageBuffer || type == ShaderResourceType::AccelerationStructure;
}

static VkDescriptorSetAndBindingMappingEXT MakeMapping(const ShaderResourceBinding& binding, VkDescriptorMappingSourceEXT source)
{
    VkDescriptorSetAndBindingMappingEXT mapping = {};
    mapping.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
    mapping.descriptorSet = binding.set;
    mapping.firstBinding = binding.binding;
    mapping.bindingCount = 1;
    mapping.resourceMask = ResourceMask(binding.type);
    mapping.source = source;

    return mapping;
}

static uint32_t ExecutionModel(VkShaderStageFlagBits stage)
{
    switch (stage)
    {
    case VK_SHADER_STAGE_RAYGEN_BIT_KHR:      return spv::ExecutionModelRayGenerationKHR;
    case VK_SHADER_STAGE_MISS_BIT_KHR:        return spv::ExecutionModelMissKHR;
    case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR: return spv::ExecutionModelClosestHitKHR;
    case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:     return spv::ExecutionModelAnyHitKHR;
    default:                                  return spv::ExecutionModelMax;
    }
}

void RayTracingGlobalResources::AddPushData(const char* name, uint32_t pushOffset, uint32_t size)
{
    m_Resources.push_back({ name, Source::PushData, pushOffset, size });
}

void RayTracingGlobalResources::AddPushAddress(const char* name, uint32_t pushOffset)
{
    m_Resources.push_back({ name, Source::PushAddress, pushOffset, 0 });
}

void RayTracingGlobalResources::AddHeapResource(const char* name, VkDeviceSize heapOffset)
{
    m_Resources.push_back({ name, Source::Heap, (uint32_t)heapOffset, 0 });
}

const RayTracingGlobalResources::Resource* RayTracingGlobalResources::Find(const std::string& name) const
{
    for (const Resource& resource : m_Resources)
        if (resource.name == name)
            return &resource;

    return nullptr;
}

// A resource with no register is local to the shader's record: a constant buffer holds its contents there, a buffer
// or an acceleration structure its address, and a texture the slot its descriptor is in. Each value a material can
// set becomes a property, which is every member of a constant buffer's block.
bool RayTracingShader::AddLocalResource(const ShaderResourceBinding& binding, const RayTracingGlobalResources& globals)
{
    VkDescriptorSetAndBindingMappingEXT mapping;

    if (binding.type == ShaderResourceType::ConstantBuffer && binding.size != 0)
    {
        const uint32_t offset = (uint32_t)AlignUp(m_LocalDataSize, sizeof(VkDeviceAddress));

        mapping = MakeMapping(binding, VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_DATA_EXT);
        mapping.sourceData.shaderRecordDataOffset = offset;

        // A member of a nested struct or of an array can't be reached by name: its outer member is one property of a
        // type no value can be written as, which reports the mismatch instead of writing bytes over it.
        for (const ShaderConstantMember& member : binding.members)
            m_Properties.push_back({ member.name, member.type, offset + member.offset, member.size, member.components });

        m_LocalDataSize = offset + binding.size;
    }
    else if (binding.type == ShaderResourceType::StorageBuffer || binding.type == ShaderResourceType::AccelerationStructure)
    {
        const uint32_t offset = (uint32_t)AlignUp(m_LocalDataSize, sizeof(VkDeviceAddress));

        mapping = MakeMapping(binding, VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_ADDRESS_EXT);
        mapping.sourceData.shaderRecordAddressOffset = offset;

        m_Properties.push_back({ binding.name, ShaderPropertyType::Buffer, offset, (uint32_t)sizeof(VkDeviceAddress), 1 });

        m_LocalDataSize = offset + sizeof(VkDeviceAddress);
    }
    else if (binding.type == ShaderResourceType::SampledImage || binding.type == ShaderResourceType::StorageImage)
    {
        if (globals.HeapSlotSize() == 0)
        {
            ReportError("%s.spv: %s is a texture local to the shader's record, which needs the slot size of the resource heap (RayTracingGlobalResources::SetHeapSlotSize).", m_Name.c_str(), binding.name.c_str());
            return false;
        }

        const uint32_t offset = (uint32_t)AlignUp(m_LocalDataSize, sizeof(uint32_t));

        // The record holds the slot, which the stride turns into the offset of its descriptor in the heap. The
        // sampler of a Texture2D.Sample stays a resource of its own, so nothing here binds one.
        mapping = MakeMapping(binding, VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_SHADER_RECORD_INDEX_EXT);
        mapping.sourceData.shaderRecordIndex.heapOffset          = 0;
        mapping.sourceData.shaderRecordIndex.shaderRecordOffset  = offset;
        mapping.sourceData.shaderRecordIndex.heapIndexStride     = globals.HeapSlotSize();
        mapping.sourceData.shaderRecordIndex.heapArrayStride     = globals.HeapSlotSize();

        m_Properties.push_back({ binding.name, ShaderPropertyType::Texture, offset, (uint32_t)sizeof(uint32_t), 1 });

        m_LocalDataSize = offset + sizeof(uint32_t);
    }
    else
    {
        ReportError("%s.spv: %s has no register, which makes it local to the shader's record, but only constant buffers, buffers, acceleration structures and textures can be.", m_Name.c_str(), binding.name.c_str());
        return false;
    }

    m_Mappings.push_back(mapping);

    return true;
}

bool RayTracingShader::Load(const char* name, const RayTracingGlobalResources& globals)
{
    Destroy();

    m_Name = name;

    const std::vector<uint32_t> spirv = LoadSpirv(name);

    if (spirv.empty())
    {
        ReportError("%s.spv: couldn't load the shader.", name);
        return false;
    }

    m_Module = CreateShaderModule(spirv, name);

    if (m_Module == VK_NULL_HANDLE)
        return false;

    m_Reflection = ShaderReflection(spirv);

    for (const ShaderResourceBinding& binding : m_Reflection.Bindings())
    {
        char message[256];
        _snprintf_s(message, _TRUNCATE, "%s.spv: %s = set %u, binding %u\n", name, binding.name.c_str(), binding.set, binding.binding);
        OutputDebugString(message);

        if (binding.set == kLocalResourceSet)
        {
            // Local: what the shader reads is in its own record, so the record's layout decides where.
            if (!AddLocalResource(binding, globals))
                return false;

            continue;
        }

        const RayTracingGlobalResources::Resource* global = globals.Find(binding.name);

        if (global == nullptr)
        {
            ReportError("%s.spv: %s (set %u, binding %u) is not one of the global resources.", name, binding.name.c_str(), binding.set, binding.binding);
            return false;
        }

        VkDescriptorSetAndBindingMappingEXT mapping;

        switch (global->source)
        {
        case RayTracingGlobalResources::Source::PushData:
            if (binding.type != ShaderResourceType::ConstantBuffer || binding.size != global->size)
            {
                ReportError("%s.spv: %s must be a constant buffer of %u bytes to be read from the push data.", name, binding.name.c_str(), global->size);
                return false;
            }

            mapping = MakeMapping(binding, VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_DATA_EXT);
            mapping.sourceData.pushDataOffset = global->offset;
            break;

        case RayTracingGlobalResources::Source::PushAddress:
            if (!HasAddress(binding.type))
            {
                ReportError("%s.spv: %s must be a buffer or an acceleration structure to be bound by its address.", name, binding.name.c_str());
                return false;
            }

            mapping = MakeMapping(binding, VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT);
            mapping.sourceData.pushAddressOffset = global->offset;
            break;

        default:
            if (binding.type == ShaderResourceType::Unknown)
            {
                ReportError("%s.spv: %s has a type that can't be bound from the resource heap.", name, binding.name.c_str());
                return false;
            }

            mapping = MakeMapping(binding, VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT);
            mapping.sourceData.constantOffset.heapOffset = global->offset;
            break;
        }

        m_Mappings.push_back(mapping);
    }

    m_LocalDataSize = (uint32_t)AlignUp(m_LocalDataSize, sizeof(VkDeviceAddress));

    return true;
}

void RayTracingShader::Destroy()
{
    if (m_Module != VK_NULL_HANDLE)
        vkDestroyShaderModule(g_VkDevice, m_Module, nullptr);

    m_Name.clear();
    m_Module = VK_NULL_HANDLE;
    m_Reflection = {};
    m_LocalDataSize = 0;
    m_DefaultProperties.Clear();
    m_Mappings.clear();
    m_Properties.clear();
}

bool RayTracingShader::HasEntryPoint(const std::string& entryPoint, VkShaderStageFlagBits stage) const
{
    if (m_Reflection.HasEntryPoint(entryPoint, ExecutionModel(stage)))
        return true;

    ReportError("%s.spv: no entry point %s of the expected stage.", m_Name.c_str(), entryPoint.c_str());

    return false;
}

const RayTracingShaderProperty* RayTracingShader::FindProperty(const std::string& name) const
{
    for (const RayTracingShaderProperty& property : m_Properties)
        if (property.name == name)
            return &property;

    return nullptr;
}

bool RayTracingShader::WriteProperties(uint8_t* localData, const ShaderProperties& properties) const
{
    bool valid = true;

    for (const ShaderProperties::Property& value : properties.Properties())
    {
        const RayTracingShaderProperty* property = FindProperty(value.name);

        // Not a property of this shader: it reads it under no name, or doesn't read it at all, and DXC removes the
        // resources a module never uses. A block is set up for whatever shader it is used with, so this is no error.
        if (property == nullptr)
            continue;

        const uint32_t valueStride    = value.components != 0 ? value.size / value.components : 0;
        const uint32_t propertyStride = property->components != 0 ? property->size / property->components : 0;

        if (property->type != value.type || propertyStride == 0 || propertyStride != valueStride)
        {
            ReportError("%s.spv: %s is %s in the record, which %s can't be written to.", m_Name.c_str(), value.name.c_str(),
                        PropertyTypeName(property->type, property->components), PropertyTypeName(value.type, value.components));
            valid = false;
            continue;
        }

        // Fewer components than the shader reads leave the rest as they were, zero in a record laid out from scratch;
        // more are dropped. So a float3 fills the first three of a float4 and a float4 fills a float3, as in Unity.
        const uint32_t components = property->components < value.components ? property->components : value.components;

        memcpy(localData + property->offset, properties.Value(value), components * propertyStride);
    }

    return valid;
}

bool RayTracingRayGenShader::Create(const char* name, const char* rayGenEntryPoint, const char* const missEntryPoints[shared::RAY_TYPE_COUNT], const RayTracingGlobalResources& globals)
{
    if (!Load(name, globals))
        return false;

    m_RayGenEntryPoint = rayGenEntryPoint;
    m_LocalData.assign(LocalDataSize(), 0);

    bool valid = HasEntryPoint(m_RayGenEntryPoint, VK_SHADER_STAGE_RAYGEN_BIT_KHR);

    for (uint32_t rayType = 0; rayType < shared::RAY_TYPE_COUNT; rayType++)
    {
        m_MissEntryPoints[rayType] = missEntryPoints[rayType];
        valid = HasEntryPoint(m_MissEntryPoints[rayType], VK_SHADER_STAGE_MISS_BIT_KHR) && valid;
    }

    return valid;
}

bool RayTracingRayGenShader::SetProperties(const ShaderProperties& properties)
{
    m_LocalData.assign(LocalDataSize(), 0);

    return m_LocalData.empty() || WriteProperties(m_LocalData.data(), properties);
}

bool RayTracingSurfaceShader::Create(const char* name, const RayTracingHitGroup hitGroups[shared::RAY_TYPE_COUNT], const RayTracingGlobalResources& globals)
{
    if (!Load(name, globals))
        return false;

    bool valid = true;

    for (uint32_t rayType = 0; rayType < shared::RAY_TYPE_COUNT; rayType++)
    {
        m_HitGroups[rayType] = hitGroups[rayType];

        if (!m_HitGroups[rayType].closestHit.empty())
            valid = HasEntryPoint(m_HitGroups[rayType].closestHit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) && valid;

        if (!m_HitGroups[rayType].anyHit.empty())
            valid = HasEntryPoint(m_HitGroups[rayType].anyHit, VK_SHADER_STAGE_ANY_HIT_BIT_KHR) && valid;
    }

    return valid;
}

bool RayTracingSurfaceShader::IsOpaque() const
{
    for (const RayTracingHitGroup& hitGroup : m_HitGroups)
        if (!hitGroup.anyHit.empty())
            return false;

    return true;
}
