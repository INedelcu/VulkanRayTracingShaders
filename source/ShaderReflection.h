#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal SPIR-V reflection: the DescriptorSet / Binding decorations of every named resource variable in a module.
// The command line in the Custom Build tool is:
// "$(DxcPath)" -T lib_6_3 -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_KHR_ray_tracing -fvk-use-scalar-layout -auto-binding-space 0
// -HV 2021 -I "$(MSBuildProjectDirectory)\shaders" -I "$(MSBuildProjectDirectory)\source" -Fo "$(ShaderOutDir)%(Filename).spv" "%(FullPath)"
struct ShaderResourceBinding
{
    std::string name;
    uint32_t    set     = 0;
    uint32_t    binding = 0;
};

class ShaderReflection
{
public:
    ShaderReflection() = default;
    explicit ShaderReflection(const std::vector<uint32_t>& spirv);

    // Null when the module has no such resource (DXC removes the ones a module never uses).
    const ShaderResourceBinding* Find(const std::string& name) const;

    const std::vector<ShaderResourceBinding>& Bindings() const { return m_Bindings; }

private:
    std::vector<ShaderResourceBinding> m_Bindings; // sorted by set, then binding
};
