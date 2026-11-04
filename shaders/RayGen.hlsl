#include "SharedTypes.h"

RaytracingAccelerationStructure AccelStruct : register(t0, space1);
RWTexture2D<float4> Output : register(u1, space1);
ConstantBuffer<FrameConstants> Frame : register(b0, space1);

static const float3 kSkyHorizon = float3(0.60f, 0.75f, 0.90f);
static const float3 kSkyZenith = float3(0.07, 0.21, 0.69);

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
    float t = smoothstep(0.0f, 0.5f, WorldRayDirection().y);

    payload.color = lerp(kSkyHorizon, kSkyZenith, t);
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    payload.color = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
}
