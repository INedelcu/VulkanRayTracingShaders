#include "ShaderReflection.h"

#include <spirv-headers/spirv.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace
{
struct Type
{
    uint32_t              opcode = 0;
    std::vector<uint32_t> operands; // the words after the result id
};

struct Variable
{
    uint32_t type         = 0;
    uint32_t storageClass = 0;
};

struct MemberLayout
{
    uint32_t offset       = 0;
    uint32_t matrixStride = 0;
    bool     rowMajor     = false;
};

struct Module
{
    std::unordered_map<uint32_t, std::string> names;
    std::unordered_map<uint32_t, uint32_t>    sets;
    std::unordered_map<uint32_t, uint32_t>    bindings;
    std::unordered_map<uint32_t, uint32_t>    arrayStrides;
    std::unordered_map<uint32_t, uint32_t>    constants; // the first value word, enough for array lengths
    std::unordered_map<uint32_t, Type>        types;
    std::unordered_map<uint32_t, Variable>    variables;
    std::unordered_set<uint32_t>              bufferBlocks;

    std::map<std::pair<uint32_t, uint32_t>, MemberLayout> members;     // by struct id and member index
    std::map<std::pair<uint32_t, uint32_t>, std::string>  memberNames; // by struct id and member index

    const Type* FindType(uint32_t id) const
    {
        auto type = types.find(id);

        return type != types.end() ? &type->second : nullptr;
    }

    // Bytes a value of the type takes in a buffer. The layout is the struct member's the value is, or is an array in.
    uint32_t Size(uint32_t id, const MemberLayout* layout = nullptr) const
    {
        const Type* type = FindType(id);

        if (type == nullptr)
            return 0;

        const std::vector<uint32_t>& operands = type->operands;

        switch (type->opcode)
        {
        case spv::OpTypeInt:
        case spv::OpTypeFloat:
            return operands[0] / 8;

        case spv::OpTypeVector:
            return Size(operands[0]) * operands[1];

        case spv::OpTypeMatrix:
        {
            const Type* column = FindType(operands[0]);

            if (column == nullptr || column->opcode != spv::OpTypeVector)
                return 0;

            const uint32_t columns = operands[1];
            const uint32_t rows    = column->operands[1];
            const uint32_t scalar  = Size(column->operands[0]);
            const bool     rowMajor = layout != nullptr && layout->rowMajor;

            // The stride separates the vectors stored one after another: rows when row major, columns otherwise.
            const uint32_t vectorCount = rowMajor ? rows : columns;
            const uint32_t vectorSize  = (rowMajor ? columns : rows) * scalar;
            const uint32_t stride      = layout != nullptr && layout->matrixStride != 0 ? layout->matrixStride : vectorSize;

            return (vectorCount - 1) * stride + vectorSize;
        }

        case spv::OpTypeArray:
        {
            auto length = constants.find(operands[1]);
            auto stride = arrayStrides.find(id);

            if (length == constants.end())
                return 0;

            return length->second * (stride != arrayStrides.end() ? stride->second : Size(operands[0], layout));
        }

        case spv::OpTypeStruct:
        {
            uint32_t size = 0;

            for (uint32_t i = 0; i < operands.size(); i++)
            {
                auto member = members.find({ id, i });

                if (member != members.end())
                    size = std::max(size, member->second.offset + Size(operands[i], &member->second));
            }

            return size;
        }

        default:
            return 0;
        }
    }

    // What kind of value the type holds and how many components it has: a scalar one, the components of a vector, or
    // all the elements of a matrix. Unknown for anything a property can't hold, like a nested struct or an array.
    ShaderPropertyType PropertyType(uint32_t id, uint32_t& components) const
    {
        const Type* type = FindType(id);

        components = 0;

        if (type == nullptr)
            return ShaderPropertyType::Unknown;

        switch (type->opcode)
        {
        case spv::OpTypeFloat:
            components = 1;
            return ShaderPropertyType::Float;

        case spv::OpTypeInt:
            // OpTypeInt <result id> <width> <signedness>
            components = 1;
            return type->operands[1] != 0 ? ShaderPropertyType::Int : ShaderPropertyType::Uint;

        case spv::OpTypeVector:
        {
            // OpTypeVector <result id> <component type> <component count>
            uint32_t                 scalarComponents = 0;
            const ShaderPropertyType scalar           = PropertyType(type->operands[0], scalarComponents);

            if (scalar == ShaderPropertyType::Unknown)
                return ShaderPropertyType::Unknown;

            components = type->operands[1];
            return scalar;
        }

        case spv::OpTypeMatrix:
        {
            // OpTypeMatrix <result id> <column type> <column count>
            const Type* column = FindType(type->operands[0]);

            if (column == nullptr || column->opcode != spv::OpTypeVector)
                return ShaderPropertyType::Unknown;

            uint32_t                 scalarComponents = 0;
            const ShaderPropertyType scalar           = PropertyType(column->operands[0], scalarComponents);

            if (scalar == ShaderPropertyType::Unknown)
                return ShaderPropertyType::Unknown;

            components = type->operands[1] * column->operands[1];
            return scalar;
        }

        default:
            return ShaderPropertyType::Unknown;
        }
    }

    // The members of a block, in declaration order. A member the module has no name or no offset for is left out: it
    // can't be found by name, and without an offset there is nowhere to write it.
    void Members(uint32_t structId, std::vector<ShaderConstantMember>& members) const
    {
        const Type* type = FindType(structId);

        if (type == nullptr || type->opcode != spv::OpTypeStruct)
            return;

        for (uint32_t i = 0; i < (uint32_t)type->operands.size(); i++)
        {
            auto name   = memberNames.find({ structId, i });
            auto layout = this->members.find({ structId, i });

            if (name == memberNames.end() || layout == this->members.end())
                continue;

            ShaderConstantMember member;
            member.name   = name->second;
            member.offset = layout->second.offset;
            member.size   = Size(type->operands[i], &layout->second);
            member.type   = PropertyType(type->operands[i], member.components);

            members.push_back(member);
        }
    }

    void Classify(const Variable& variable, ShaderResourceBinding& binding) const
    {
        const Type* pointer = FindType(variable.type);

        if (pointer == nullptr || pointer->opcode != spv::OpTypePointer)
            return;

        uint32_t    typeId = pointer->operands[1];
        const Type* type   = FindType(typeId);

        // An array of resources has the type of its elements.
        while (type != nullptr && (type->opcode == spv::OpTypeArray || type->opcode == spv::OpTypeRuntimeArray))
        {
            typeId = type->operands[0];
            type   = FindType(typeId);
        }

        if (type == nullptr)
            return;

        switch (type->opcode)
        {
        case spv::OpTypeStruct:
            // Storage buffers are in the StorageBuffer class, or in Uniform with a BufferBlock struct before SPIR-V 1.3.
            if (variable.storageClass == spv::StorageClassStorageBuffer || bufferBlocks.count(typeId) != 0)
            {
                binding.type = ShaderResourceType::StorageBuffer;
            }
            else if (variable.storageClass == spv::StorageClassUniform)
            {
                binding.type = ShaderResourceType::ConstantBuffer;
                binding.size = Size(typeId);
                Members(typeId, binding.members);
            }
            break;

        case spv::OpTypeImage:
            // OpTypeImage <id> <sampled type> <dim> <depth> <arrayed> <multisampled> <sampled> <format>: sampled is 2
            // for images read and written without a sampler.
            binding.type = type->operands[5] == 2 ? ShaderResourceType::StorageImage : ShaderResourceType::SampledImage;
            break;

        case spv::OpTypeSampler:
            binding.type = ShaderResourceType::Sampler;
            break;

        case spv::OpTypeAccelerationStructureKHR:
            binding.type = ShaderResourceType::AccelerationStructure;
            break;

        default:
            break;
        }
    }
};

std::string ReadString(const uint32_t* words, uint32_t wordCount)
{
    const char* text = reinterpret_cast<const char*>(words);

    return std::string(text, strnlen(text, wordCount * sizeof(uint32_t)));
}
}

ShaderReflection::ShaderReflection(const std::vector<uint32_t>& spirv)
{
    if (spirv.size() < 5 || spirv[0] != spv::MagicNumber)
    {
        std::fprintf(stderr, "ShaderReflection: not a SPIR-V parsed.\n");
        return;
    }

    Module parsed;

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

        const uint32_t* words = &spirv[i];

        switch (opcode)
        {
        case spv::OpEntryPoint:
            // OpEntryPoint <execution model> <id> <null terminated string packed into words> <interface ids>
            if (wordCount >= 4)
                m_EntryPoints.push_back({ ReadString(words + 3, wordCount - 3), words[1] });
            break;

        case spv::OpName:
            // OpName <target id> <null terminated string packed into words>
            if (wordCount >= 3)
                parsed.names[words[1]] = ReadString(words + 2, wordCount - 2);
            break;

        case spv::OpMemberName:
            // OpMemberName <struct id> <member index> <null terminated string packed into words>
            if (wordCount >= 4)
                parsed.memberNames[{ words[1], words[2] }] = ReadString(words + 3, wordCount - 3);
            break;

        case spv::OpDecorate:
            // OpDecorate <target id> <decoration> <literals>
            if (wordCount >= 3 && words[2] == spv::DecorationBufferBlock)
                parsed.bufferBlocks.insert(words[1]);
            else if (wordCount >= 4 && words[2] == spv::DecorationDescriptorSet)
                parsed.sets[words[1]] = words[3];
            else if (wordCount >= 4 && words[2] == spv::DecorationBinding)
                parsed.bindings[words[1]] = words[3];
            else if (wordCount >= 4 && words[2] == spv::DecorationArrayStride)
                parsed.arrayStrides[words[1]] = words[3];
            break;

        case spv::OpMemberDecorate:
            // OpMemberDecorate <struct id> <member index> <decoration> <literals>
            if (wordCount >= 5 && words[3] == spv::DecorationOffset)
                parsed.members[{ words[1], words[2] }].offset = words[4];
            else if (wordCount >= 5 && words[3] == spv::DecorationMatrixStride)
                parsed.members[{ words[1], words[2] }].matrixStride = words[4];
            else if (wordCount >= 4 && words[3] == spv::DecorationRowMajor)
                parsed.members[{ words[1], words[2] }].rowMajor = true;
            break;

        case spv::OpTypeInt:
        case spv::OpTypeFloat:
        case spv::OpTypeVector:
        case spv::OpTypeMatrix:
        case spv::OpTypeImage:
        case spv::OpTypeSampler:
        case spv::OpTypeSampledImage:
        case spv::OpTypeArray:
        case spv::OpTypeRuntimeArray:
        case spv::OpTypeStruct:
        case spv::OpTypePointer:
        case spv::OpTypeAccelerationStructureKHR:
            // OpType* <result id> <operands>
            if (wordCount >= 2)
                parsed.types[words[1]] = { opcode, std::vector<uint32_t>(words + 2, words + wordCount) };
            break;

        case spv::OpConstant:
            // OpConstant <type id> <result id> <value>
            if (wordCount >= 4)
                parsed.constants[words[2]] = words[3];
            break;

        case spv::OpVariable:
            // OpVariable <type id> <result id> <storage class>
            if (wordCount >= 4)
                parsed.variables[words[2]] = { words[1], words[3] };
            break;

        default:
            break;
        }

        i += wordCount;
    }

    for (const auto& [id, bindingIndex] : parsed.bindings)
    {
        auto set      = parsed.sets.find(id);
        auto name     = parsed.names.find(id);
        auto variable = parsed.variables.find(id);

        if (set == parsed.sets.end() || name == parsed.names.end() || variable == parsed.variables.end())
            continue;

        ShaderResourceBinding binding;
        binding.name    = name->second;
        binding.set     = set->second;
        binding.binding = bindingIndex;

        parsed.Classify(variable->second, binding);

        m_Bindings.push_back(binding);
    }

    std::sort(m_Bindings.begin(), m_Bindings.end(), [](const ShaderResourceBinding& a, const ShaderResourceBinding& b) {
        return a.set != b.set ? a.set < b.set : a.binding < b.binding;
    });
}

const ShaderResourceBinding* ShaderReflection::Find(const std::string& name) const
{
    for (const ShaderResourceBinding& binding : m_Bindings)
        if (binding.name == name)
            return &binding;
    return nullptr;
}

bool ShaderReflection::HasEntryPoint(const std::string& name, uint32_t executionModel) const
{
    for (const ShaderEntryPoint& entryPoint : m_EntryPoints)
        if (entryPoint.name == name && entryPoint.executionModel == executionModel)
            return true;
    return false;
}
