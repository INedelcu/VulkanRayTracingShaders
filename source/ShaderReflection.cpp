#include "ShaderReflection.h"

#include <spirv-headers/spirv.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

ShaderReflection::ShaderReflection(const std::vector<uint32_t>& spirv)
{
    if (spirv.size() < 5 || spirv[0] != spv::MagicNumber)
    {
        std::fprintf(stderr, "ShaderReflection: not a SPIR-V module.\n");
        return;
    }

    std::unordered_map<uint32_t, std::string> names;
    std::unordered_map<uint32_t, uint32_t>    sets;
    std::unordered_map<uint32_t, uint32_t>    bindings;

    // Instructions follow the 5 word header: word 0 holds the word count and the opcode.
    for (size_t i = 5; i < spirv.size();)
    {
        uint32_t wordCount = spirv[i] >> spv::WordCountShift;
        uint32_t opcode    = spirv[i] & spv::OpCodeMask;
        if (wordCount == 0 || i + wordCount > spirv.size())
        {
            std::fprintf(stderr, "ShaderReflection: malformed instruction at word %zu.\n", i);
            break;
        }

        if (opcode == spv::OpName && wordCount >= 3)
        {
            // OpName <target id> <null terminated string packed into words>
            const char* text = reinterpret_cast<const char*>(&spirv[i + 2]);
            names[spirv[i + 1]] = std::string(text, strnlen(text, (wordCount - 2) * sizeof(uint32_t)));
        }
        else if (opcode == spv::OpDecorate && wordCount >= 4)
        {
            // OpDecorate <target id> <decoration> <literal>
            if (spirv[i + 2] == spv::DecorationDescriptorSet)
                sets[spirv[i + 1]] = spirv[i + 3];
            else if (spirv[i + 2] == spv::DecorationBinding)
                bindings[spirv[i + 1]] = spirv[i + 3];
        }
        i += wordCount;
    }

    for (const auto& [id, binding] : bindings)
    {
        auto set  = sets.find(id);
        auto name = names.find(id);
        if (set != sets.end() && name != names.end())
            m_bindings.push_back({name->second, set->second, binding});
    }
    std::sort(m_bindings.begin(), m_bindings.end(), [](const ShaderResourceBinding& a, const ShaderResourceBinding& b) {
        return a.set != b.set ? a.set < b.set : a.binding < b.binding;
    });
}

const ShaderResourceBinding* ShaderReflection::Find(const std::string& name) const
{
    for (const ShaderResourceBinding& binding : m_bindings)
        if (binding.name == name)
            return &binding;
    return nullptr;
}
