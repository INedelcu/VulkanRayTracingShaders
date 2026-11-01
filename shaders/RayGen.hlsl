#include "SharedTypes.h"

RaytracingAccelerationStructure Scene : register(t0, space1);
RWTexture2D<float4> Output : register(u1, space1);

struct Payload
{
    float3 color;
};

[shader("raygeneration")]
void RayGen()
{
    uint2  pixel = DispatchRaysIndex().xy;
    float2 uv    = (float2(pixel) + 0.5f) / float2(DispatchRaysDimensions().xy);

    RayDesc ray;
    ray.Origin    = float3(0, 0, 0);
    ray.Direction = float3(0, 0, -1);
    ray.TMin      = 0.0f;
    ray.TMax      = 10000.0f;

    Payload payload;
    payload.color = float3(uv, 0.0f);
    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

    Output[pixel] = float4(payload.color, 1.0f);
}
