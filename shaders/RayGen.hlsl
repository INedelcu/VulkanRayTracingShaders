#include "SharedTypes.h"
#include "TriangleMesh.hlsli"

RaytracingAccelerationStructure AccelStruct : register(t0, space1);
RWTexture2D<float4> Output : register(u1, space1);
ConstantBuffer<FrameConstants> Frame : register(b0, space1);

static const float3 kSkyHorizon = float3(0.60f, 0.75f, 0.90f);
static const float3 kSkyZenith = float3(0.07, 0.21, 0.69);

struct RadiancePayload
{
    float3 color;  // light that needs no shadow test: sky, ambient
    float3 direct; // sun light, added only if the sun is visible from origin
    float3 origin; // hit position moved off the surface
};

struct ShadowPayload
{
    float visibility;
};

static const float3 kSunDirection = normalize(float3(-0.45f, 0.75f, 0.42f));
static const float3 kAlbedo       = float3(0.95f, 0.95f, 0.95f);
static const float3 kAmbient      = float3(0.1f, 0.18f, 0.25f);

// "A Fast and Robust Method for Avoiding Self-Intersection", Ray Tracing Gems 2019.
float3 OffsetRayOrigin(float3 p, float3 n)
{
    const float originThreshold = 1.0f / 32.0f;
    const float floatScale      = 1.0f / 65536.0f;
    const float intScale        = 256.0f;

    int3   offsetInt = int3(intScale * n);
    float3 pInt      = asfloat(asint(p) + select(p < 0.0f, -offsetInt, offsetInt));

    return select(abs(p) < originThreshold, p + floatScale * n, pInt);
}

bool IsSunVisible(float3 origin)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = kSunDirection;
    ray.TMin      = 0.0f;
    ray.TMax      = 10000.0f;

    ShadowPayload payload;
    payload.visibility = 0.0f;
    TraceRay(AccelStruct, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, RAY_TYPE_SHADOW, RAY_TYPE_COUNT, RAY_TYPE_SHADOW, ray, payload);

    return payload.visibility > 0.0f;
}

float3 TraceCameraRay(float2 pixelPosition)
{
    float2 ndc        = pixelPosition / float2(DispatchRaysDimensions().xy) * 2.0f - 1.0f;
    float  tanHalfFov = Frame.cameraPosition.w;
    float  aspect     = Frame.cameraRight.w;

    RayDesc ray;
    ray.Origin    = Frame.cameraPosition.xyz;
    ray.Direction = normalize(Frame.cameraForward.xyz + Frame.cameraRight.xyz * (ndc.x * tanHalfFov * aspect) - Frame.cameraUp.xyz * (ndc.y * tanHalfFov));
    ray.TMin      = 0.0f;
    ray.TMax      = 10000.0f;

    RadiancePayload payload;
    payload.color  = float3(0.0f, 0.0f, 0.0f);
    payload.direct = float3(0.0f, 0.0f, 0.0f);
    payload.origin = float3(0.0f, 0.0f, 0.0f);
    TraceRay(AccelStruct, RAY_FLAG_NONE, 0xFF, RAY_TYPE_RADIANCE, RAY_TYPE_COUNT, RAY_TYPE_RADIANCE, ray, payload);

    float3 color = payload.color;

    if (any(payload.direct > 0.0f) && IsSunVisible(payload.origin))
        color += payload.direct;

    return color;
}

[shader("raygeneration")]
void RayGen()
{
    uint2  pixel = DispatchRaysIndex().xy;

    float3 color = TraceCameraRay(float2(pixel) + 0.5f);

    Output[pixel] = float4(color, 1.0f);
}

[shader("miss")]
void MissRadiance(inout RadiancePayload payload)
{
    float t = smoothstep(0.0f, 0.5f, WorldRayDirection().y);

    payload.color = lerp(kSkyHorizon, kSkyZenith, t);
}

[shader("miss")]
void MissShadow(inout ShadowPayload payload)
{
    payload.visibility = 1.0f;
}

[shader("closesthit")]
void ClosestHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    SurfaceHit hit = FetchSurface(attributes);

    float diffuse = saturate(dot(hit.shadingNormal, kSunDirection));

    // Lit, the two parts add up to kAlbedo * lerp(kAmbient, 1, diffuse); in shadow only the ambient part is left.
    payload.color  = kAlbedo * kAmbient;
    payload.direct = kAlbedo * (1.0f - kAmbient) * diffuse;
    payload.origin = OffsetRayOrigin(hit.smoothPosition, hit.geometricNormal);
}
