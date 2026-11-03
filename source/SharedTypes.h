// Types shared between the C++ host code and the HLSL shaders.
#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#ifdef __cplusplus
#include <cstdint>
namespace shared
{
using uint = uint32_t;
struct float2 { float x, y; };
struct float3 { float x, y, z; };
struct float4 { float x, y, z, w; };
#endif

struct Vertex
{
    float3 position;
    float3 normal;
    float2 uv;
};

struct FrameConstants
{
    float4 cameraPosition; // xyz: eye position, w: tan(verticalFov / 2)
    float4 cameraRight;    // xyz: right axis, w: aspect ratio (width / height)
    float4 cameraUp;       // xyz: up axis
    float4 cameraForward;  // xyz: forward axis
};

#ifdef __cplusplus
} // namespace shared
#endif

#endif // SHARED_TYPES_H
