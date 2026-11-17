#include "ShaderProperties.h"

#include <cstring>

void ShaderProperties::SetFloat(const char* name, float value)
{
    Set(name, ShaderPropertyType::Float, 1, &value, sizeof(value));
}

void ShaderProperties::SetVector(const char* name, const Vec2& value)
{
    const float values[2] = { value.x, value.y };

    Set(name, ShaderPropertyType::Float, 2, values, sizeof(values));
}

void ShaderProperties::SetVector(const char* name, const Vec3& value)
{
    const float values[3] = { value.x, value.y, value.z };

    Set(name, ShaderPropertyType::Float, 3, values, sizeof(values));
}

void ShaderProperties::SetVector(const char* name, const Vec4& value)
{
    const float values[4] = { value.x, value.y, value.z, value.w };

    Set(name, ShaderPropertyType::Float, 4, values, sizeof(values));
}

void ShaderProperties::SetInt(const char* name, int32_t value)
{
    Set(name, ShaderPropertyType::Int, 1, &value, sizeof(value));
}

void ShaderProperties::SetUint(const char* name, uint32_t value)
{
    Set(name, ShaderPropertyType::Uint, 1, &value, sizeof(value));
}

void ShaderProperties::SetTexture(const char* name, uint32_t heapSlot)
{
    Set(name, ShaderPropertyType::Texture, 1, &heapSlot, sizeof(heapSlot));
}

void ShaderProperties::SetBuffer(const char* name, uint64_t address)
{
    Set(name, ShaderPropertyType::Buffer, 1, &address, sizeof(address));
}

void ShaderProperties::Clear()
{
    m_Properties.clear();
    m_Store.clear();
}

void ShaderProperties::Set(const char* name, ShaderPropertyType type, uint32_t components, const void* value, uint32_t size)
{
    for (Property& property : m_Properties)
    {
        if (property.name != name)
            continue;

        if (property.size != size)
        {
            property.offset = (uint32_t)m_Store.size();
            m_Store.resize(m_Store.size() + size);
        }

        property.type       = type;
        property.size       = size;
        property.components = components;

        memcpy(m_Store.data() + property.offset, value, size);

        return;
    }

    Property property;
    property.name       = name;
    property.type       = type;
    property.offset     = (uint32_t)m_Store.size();
    property.size       = size;
    property.components = components;

    m_Store.resize(m_Store.size() + size);
    memcpy(m_Store.data() + property.offset, value, size);

    m_Properties.push_back(property);
}
