#include "SharedTypes.h"
#include "TriangleMesh.hlsli"

RaytracingAccelerationStructure AccelStruct : register(t0, space1);
RWTexture2D<float4> Output : register(u1, space1);
ConstantBuffer<FrameConstants> Frame : register(b0, space1);

static const float3 kSkyHorizon = float3(0.60f, 0.75f, 0.90f);
static const float3 kSkyZenith = float3(0.07, 0.21, 0.69);

struct Payload
{
    float3 color;
};

static const float3 kSunDirection = float3(-0.45f, 0.75f, 0.42f);
static const float3 kAlbedo       = float3(0.95f, 0.95f, 0.95f);
static const float3 kAmbient      = float3(0.1f, 0.18f, 0.25f);

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
    SurfaceHit hit = FetchSurface(attributes);

    float diffuse = saturate(dot(hit.shadingNormal, normalize(kSunDirection)));

    // Blend from 100% ambient color (unlit side) up to full light (facing the sun).
    float3 lighting = lerp(kAmbient, float3(1.0f, 1.0f, 1.0f), diffuse);

    payload.color = kAlbedo * lighting;
}
