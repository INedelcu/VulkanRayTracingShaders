#pragma once

#include "MathUtils.h"
#include "ShaderReflection.h"

#include <cstdint>
#include <string>
#include <vector>

// The values of a shader's properties by name, each in a store of its own: a float, a vector, the slot of a texture's
// descriptor in the resource heap, or the device address of a buffer. Unity's MaterialPropertyBlock, and like it this
// knows of no shader at all: it holds only what was set, and the names are matched against the properties a shader
// reflects when the values are written into that shader's records (RayTracingShader::WriteProperties). So one block
// can be used with any shader, a name a shader doesn't read is ignored there, and a shader keeps its own layout.
class ShaderProperties
{
public:
    // One value in the store.
    struct Property
    {
        std::string        name;
        ShaderPropertyType type       = ShaderPropertyType::Unknown;
        uint32_t           offset     = 0; // bytes into the store
        uint32_t           size       = 0; // bytes the value takes
        uint32_t           components = 0; // 1 for a float or an int, 2 to 4 for a vector, 1 for a texture or a buffer
    };

    void SetFloat(const char* name, float value);
    void SetVector(const char* name, const Vec3& value);
    void SetVector(const char* name, const Vec4& value);
    void SetInt(const char* name, int32_t value);
    void SetUint(const char* name, uint32_t value);

    // The slot the texture's descriptor is in, from DescriptorHeap::Allocate.
    void SetTexture(const char* name, uint32_t heapSlot);

    // The device address of a buffer or an acceleration structure, a VkDeviceAddress.
    void SetBuffer(const char* name, uint64_t address);

    void Clear();

    bool IsEmpty() const { return m_Properties.empty(); }

    const std::vector<Property>& Properties() const { return m_Properties; }

    // The bytes of one of this block's properties, property.size of them.
    const uint8_t* Value(const Property& property) const { return m_Store.data() + property.offset; }

private:
    // Keeps the last value set under a name. Setting a name again with a value of another size leaves the bytes of
    // the old one unused: a block holds a handful of values and is written once per material.
    void Set(const char* name, ShaderPropertyType type, uint32_t components, const void* value, uint32_t size);

    std::vector<Property> m_Properties;
    std::vector<uint8_t>  m_Store;
};
