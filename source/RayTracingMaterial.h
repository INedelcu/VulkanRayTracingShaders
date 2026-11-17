#pragma once

#include "RayTracingShader.h"

#include <cstdint>
#include <vector>

// A surface shader and the values of its properties. Instances point to a material; several can share one.
// Without a shader no hit shader runs for the instances: they stop the rays that hit them and nothing else.
class RayTracingMaterial
{
public:
    RayTracingMaterial() = default;

    // Starts with the values the shader gives a material of its own, RayTracingShader::DefaultProperties.
    explicit RayTracingMaterial(const RayTracingSurfaceShader* shader)
        : m_Shader(shader), m_Properties(shader != nullptr ? shader->DefaultProperties() : ShaderProperties())
    {
    }

    // Null when the material has no shader.
    const RayTracingSurfaceShader* Shader() const { return m_Shader; }

    // The value of one of the shader's properties, by the name the shader declares it with, e.g.
    // SetFloat("roughness", 0.3f). Returns the material so the values of one can be set in a chain.
    RayTracingMaterial& SetFloat(const char* name, float value) { m_Properties.SetFloat(name, value); return *this; }
    RayTracingMaterial& SetVector(const char* name, const Vec2& value) { m_Properties.SetVector(name, value); return *this; }
    RayTracingMaterial& SetVector(const char* name, const Vec3& value) { m_Properties.SetVector(name, value); return *this; }
    RayTracingMaterial& SetVector(const char* name, const Vec4& value) { m_Properties.SetVector(name, value); return *this; }
    RayTracingMaterial& SetInt(const char* name, int32_t value) { m_Properties.SetInt(name, value); return *this; }
    RayTracingMaterial& SetUint(const char* name, uint32_t value) { m_Properties.SetUint(name, value); return *this; }
    RayTracingMaterial& SetTexture(const char* name, uint32_t heapSlot) { m_Properties.SetTexture(name, heapSlot); return *this; }
    RayTracingMaterial& SetBuffer(const char* name, VkDeviceAddress address) { m_Properties.SetBuffer(name, address); return *this; }

    const ShaderProperties& Properties() const { return m_Properties; }

    // Lays out the local data of one instance's hit record: this material's values, then the ones only that instance
    // has, like the buffers of its mesh. A property the shader doesn't read is skipped, and one it reads that neither
    // block sets is left zero. Empty without a shader, whose records hold no local data.
    bool WriteLocalData(std::vector<uint8_t>& localData, const ShaderProperties* instanceProperties = nullptr) const
    {
        localData.assign(m_Shader != nullptr ? m_Shader->LocalDataSize() : 0, 0);

        if (m_Shader == nullptr || localData.empty())
            return true;

        bool valid = m_Shader->WriteProperties(localData.data(), m_Properties);

        if (instanceProperties != nullptr)
            valid = m_Shader->WriteProperties(localData.data(), *instanceProperties) && valid;

        return valid;
    }

private:
    const RayTracingSurfaceShader* m_Shader = nullptr;
    ShaderProperties               m_Properties;
};
