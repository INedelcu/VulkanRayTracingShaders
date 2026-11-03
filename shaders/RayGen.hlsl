#include "SharedTypes.h"

RaytracingAccelerationStructure AccelStruct : register(t0, space1);
RWTexture2D<float4> Output : register(u1, space1);
ConstantBuffer<FrameConstants> Frame : register(b0, space1);

struct Payload
{
    float3 color;
};

[shader("raygeneration")]
void RayGen()
{
    uint2  pixel      = DispatchRaysIndex().xy;
    float2 ndc        = (float2(pixel) + 0.5f) / float2(DispatchRaysDimensions().xy) * 2.0f - 1.0f;
    float  tanHalfFov = Frame.cameraPosition.w;
    float  aspect     = Frame.cameraRight.w;

    RayDesc ray;
    ray.Origin    = Frame.cameraPosition.xyz;
    ray.Direction = normalize(Frame.cameraForward.xyz + Frame.cameraRight.xyz * (ndc.x * tanHalfFov * aspect) - Frame.cameraUp.xyz * (ndc.y * tanHalfFov));
    ray.TMin      = 0.0f;
    ray.TMax      = 10000.0f;

    Payload payload;
    payload.color = float3(0.0f, 0.0f, 0.0f);
    TraceRay(AccelStruct, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

    Output[pixel] = float4(payload.color, 1.0f);
}

[shader("miss")]
void Miss(inout Payload payload)
{
    float t = saturate(WorldRayDirection().y * 0.5f + 0.5f);

    payload.color = lerp(float3(0.85f, 0.88f, 0.92f), float3(0.3f, 0.5f, 0.85f), t);
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    payload.color = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
}
