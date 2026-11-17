#pragma once

#include "VulkanImages.h"

// Uploads 8 bit RGBA pixels as an sRGB texture in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ready for a sampled image
// descriptor in the resource heap. One mip level: a ray has no neighbors to take derivatives from, so the shaders
// sample level 0 and nothing below it would be chosen.
Image CreateTexture(const void* rgba, VkExtent2D extent, const char* name);

// The same, from a file in the textures folder next to the executable. Any format Windows Imaging Component reads
// (.png, .jpg, .bmp, .tif, ...) works, whatever its bit depth or channels: it is converted to 8 bit RGBA on the way
// in. The returned image is empty when the file can't be read.
Image LoadTexture(const char* name);

// A 1x1 opaque white texture, what a material samples where it has no texture of its own: a shader can then multiply
// by it unconditionally instead of branching on whether one is bound.
Image CreateWhiteTexture();
