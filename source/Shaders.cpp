#include "Shaders.h"

#include <fstream>
#include <string>

static std::string GetShaderDirectory()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    std::string directory = exePath;
    return directory.substr(0, directory.find_last_of('\\') + 1) + "shaders\\";
}

std::vector<uint32_t> LoadSpirv(const char* name)
{
    std::ifstream file(GetShaderDirectory() + name + ".spv", std::ios::binary | std::ios::ate);
    if (!file)
        return {};

    const size_t size = (size_t)file.tellg();
    if (size == 0 || size % sizeof(uint32_t) != 0)
        return {};

    std::vector<uint32_t> spirv(size / sizeof(uint32_t));
    file.seekg(0);
    file.read((char*)spirv.data(), (std::streamsize)size);

    return spirv;
}

VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv, const char* name)
{
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(g_VkDevice, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    SetVkObjectName((uint64_t)shaderModule, VK_OBJECT_TYPE_SHADER_MODULE, name);

    return shaderModule;
}
