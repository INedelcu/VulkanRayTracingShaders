#include "SharedTypes.h"

RaytracingAccelerationStructure AccelStruct : register(t0, space1);
RWTexture2D<float4> Output : register(u1, space1);

struct Payload
{
    float3 color;
};

static const float3 kCameraPosition     = float3(0.0f, 2.5f, 9.0f);
static const float3 kCameraTarget       = float3(0.0f, 1.0f, 0.0f);
static const float  kVerticalFovDegrees = 60.0f;

[shader("raygeneration")]
void RayGen()
{
    uint2  pixel      = DispatchRaysIndex().xy;
    float2 resolution = float2(DispatchRaysDimensions().xy);
    float2 ndc        = (float2(pixel) + 0.5f) / resolution * 2.0f - 1.0f;
    float  tanHalfFov = tan(radians(kVerticalFovDegrees) * 0.5f);

    float3 forward = normalize(kCameraTarget - kCameraPosition);
    float3 right   = normalize(cross(forward, float3(0.0f, 1.0f, 0.0f)));
    float3 up      = cross(right, forward);

    RayDesc ray;
    ray.Origin    = kCameraPosition;
    ray.Direction = normalize(forward + right * (ndc.x * tanHalfFov * resolution.x / resolution.y) - up * (ndc.y * tanHalfFov));
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
