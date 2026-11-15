#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal SPIR-V reflection: every named resource variable of a module with its DescriptorSet / Binding decorations,
// its type and, for constant buffers, its size and the members it holds; and the module's entry points.
// The command line in the Custom Build tool is:
// "$(DxcPath)" -T lib_6_3 -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_KHR_ray_tracing -fvk-use-scalar-layout -fspv-use-unknown-image-format -auto-binding-space 0
// -HV 2021 -I "$(MSBuildProjectDirectory)\shaders" -I "$(MSBuildProjectDirectory)\source" -Fo "$(ShaderOutDir)%(Filename).spv" "%(FullPath)"
enum class ShaderResourceType
{
    Unknown,
    ConstantBuffer,
    StorageBuffer,
    SampledImage,
    StorageImage,
    Sampler,
    AccelerationStructure,
};

// What kind of value a property holds. The numeric types are members of a constant buffer; a texture is a descriptor
// in the resource heap, held as the slot it is in; a buffer, or an acceleration structure, is held as its device
// address.
enum class ShaderPropertyType
{
    Unknown,
    Float,
    Int,
    Uint,
    Texture,
    Buffer,
};

// A member of a constant buffer's block: one value the shader reads, found by the name it is declared with.
struct ShaderConstantMember
{
    std::string        name;
    ShaderPropertyType type       = ShaderPropertyType::Unknown; // Unknown for what a property can't hold, e.g. a nested struct
    uint32_t           offset     = 0; // bytes into the block
    uint32_t           size       = 0; // bytes the member takes
    uint32_t           components = 0; // 1 for a scalar, 2 to 4 for a vector, rows * columns for a matrix
};

struct ShaderResourceBinding
{
    std::string                       name;
    uint32_t                          set     = 0;
    uint32_t                          binding = 0;
    ShaderResourceType                type    = ShaderResourceType::Unknown;
    uint32_t                          size    = 0; // bytes of a constant buffer's block, 0 for the other types
    std::vector<ShaderConstantMember> members;      // a constant buffer's members in declaration order, empty for the other types
};

struct ShaderEntryPoint
{
    std::string name;
    uint32_t    executionModel = 0; // spv::ExecutionModel
};

class ShaderReflection
{
public:
    ShaderReflection() = default;
    explicit ShaderReflection(const std::vector<uint32_t>& spirv);

    // Null when the module has no such resource (DXC removes the ones a module never uses).
    const ShaderResourceBinding* Find(const std::string& name) const;

    bool HasEntryPoint(const std::string& name, uint32_t executionModel) const;

    const std::vector<ShaderResourceBinding>& Bindings() const { return m_Bindings; }
    const std::vector<ShaderEntryPoint>& EntryPoints() const { return m_EntryPoints; }

private:
    std::vector<ShaderResourceBinding> m_Bindings; // sorted by set, then binding
    std::vector<ShaderEntryPoint>      m_EntryPoints;
};
