#pragma once

#include "VulkanDevice.h"

#include <cstdint>
#include <vector>

std::vector<uint32_t> LoadSpirv(const char* name);
VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv, const char* name);
