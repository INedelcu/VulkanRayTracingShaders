#include "SharedTypes.h"
#include "TriangleMesh.hlsli"

RaytracingAccelerationStructure AccelStruct : register(t0, space1);
[[vk::image_format("rgba32f")]] RWTexture2D<float4> Accumulation : register(u1, space1);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> Output : register(u2, space1);
ConstantBuffer<FrameConstants> Frame : register(b0, space1);

static const float3 kSkyHorizon = float3(0.60f, 0.75f, 0.90f);
static const float3 kSkyZenith = float3(0.07, 0.21, 0.69);

struct RadiancePayload
{
    float3 emitted;   // light reaching the ray origin without a shadow test: the sky
    float3 direct;    // sun light leaving the hit, added only if the sun is visible from origin
    float3 weight;    // throughput factor of the bounce ray; zero ends the path
    float3 origin;    // hit position moved off the surface, start of the shadow and bounce rays
    float3 direction; // bounce ray direction
    uint   rngState;
};

struct ShadowPayload
{
    float visibility;
};

static const float  kPi              = 3.14159265f;
static const float3 kSunDirection    = normalize(float3(-0.45f, 0.75f, 0.42f));
static const float  kSunCosRadius    = cos(radians(2.0f)); // cosine of the sun disc's angular radius
static const float3 kSunColor        = float3(2.2f, 2.1f, 1.9f); // irradiance / pi: what a white surface facing the sun reflects
static const float3 kAlbedo          = float3(0.7f, 0.7f, 0.7f);
static const uint   kMaxPathSegments = 8;
static const float  kExposure        = 0.8f;

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

// PCG hash ("Hash Functions for GPU Rendering", JCGT 2020).
uint PcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;

    return (word >> 22u) ^ word;
}

// Uniform in [0, 1).
float Random(inout uint state)
{
    state = PcgHash(state);

    return float(state >> 8) * (1.0f / 16777216.0f);
}

// "Building an Orthonormal Basis, Revisited", JCGT 2017.
void BuildOrthonormalBasis(float3 n, out float3 t, out float3 b)
{
    float sign = n.z >= 0.0f ? 1.0f : -1.0f;
    float a    = -1.0f / (sign + n.z);
    float c    = n.x * n.y * a;

    t = float3(1.0f + sign * n.x * n.x * a, sign * c, -sign * n.x);
    b = float3(c, sign + n.y * n.y * a, -n.y);
}

// Cosine-weighted direction around n: a uniform point on the unit disk, lifted onto the hemisphere.
float3 SampleCosineHemisphere(float3 n, inout uint rng)
{
    float r   = sqrt(Random(rng));
    float phi = 2.0f * kPi * Random(rng);

    float3 t, b;
    BuildOrthonormalBasis(n, t, b);

    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0f, 1.0f - r * r)));
}

// A direction toward a random point of the sun disc, uniform over its solid angle. Points near a shadow edge see
// only part of the disc, so averaged over frames the edge becomes a penumbra as wide as the disc makes it.
float3 SampleSunDirection(inout uint rng)
{
    float cosTheta = lerp(kSunCosRadius, 1.0f, Random(rng));
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    float phi      = 2.0f * kPi * Random(rng);

    float3 t, b;
    BuildOrthonormalBasis(kSunDirection, t, b);

    return t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + kSunDirection * cosTheta;
}

bool IsSunVisible(float3 origin, inout uint rng)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = SampleSunDirection(rng);
    ray.TMin      = 0.0f;
    ray.TMax      = 10000.0f;

    ShadowPayload payload;
    payload.visibility = 0.0f;
    TraceRay(AccelStruct, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, RAY_TYPE_SHADOW, RAY_TYPE_COUNT, RAY_TYPE_SHADOW, ray, payload);

    return payload.visibility > 0.0f;
}

// Khronos PBR Neutral (2024): base colors stay as authored under neutral light; only the top of the range is compressed.
float3 PbrNeutral(float3 color)
{
    const float specularFloor    = 0.04f; // the reflection every dielectric adds, removed so an albedo of 0.8 maps to 0.8
    const float startCompression = 0.8f - specularFloor;
    const float desaturation     = 0.15f;

    // The specular floor fades in over a quadratic toe, so values near black are not pushed below zero.
    float x      = min(color.r, min(color.g, color.b));
    float offset = x < 2.0f * specularFloor ? x - x * x / (4.0f * specularFloor) : specularFloor;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));

    if (peak < startCompression)
        return color;

    const float d = 1.0f - startCompression;
    float newPeak = 1.0f - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    // The more a color was compressed, the more it is blended toward white.
    float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);

    return lerp(color, newPeak, g);
}

RayDesc CameraRay(float2 pixelPosition)
{
    float2 ndc        = pixelPosition / float2(DispatchRaysDimensions().xy) * 2.0f - 1.0f;
    float  tanHalfFov = Frame.cameraPosition.w;
    float  aspect     = Frame.cameraRight.w;

    RayDesc ray;
    ray.Origin    = Frame.cameraPosition.xyz;
    ray.Direction = normalize(Frame.cameraForward.xyz + Frame.cameraRight.xyz * (ndc.x * tanHalfFov * aspect) - Frame.cameraUp.xyz * (ndc.y * tanHalfFov));
    ray.TMin      = 0.0f;
    ray.TMax      = 10000.0f;

    return ray;
}

float3 TracePath(RayDesc ray, inout uint rng)
{
    float3 radiance   = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);

    for (uint segment = 0; segment < kMaxPathSegments; segment++)
    {
        RadiancePayload payload;
        payload.emitted   = float3(0.0f, 0.0f, 0.0f);
        payload.direct    = float3(0.0f, 0.0f, 0.0f);
        payload.weight    = float3(0.0f, 0.0f, 0.0f);
        payload.origin    = float3(0.0f, 0.0f, 0.0f);
        payload.direction = float3(0.0f, 0.0f, 0.0f);
        payload.rngState  = rng;
        TraceRay(AccelStruct, RAY_FLAG_NONE, 0xFF, RAY_TYPE_RADIANCE, RAY_TYPE_COUNT, RAY_TYPE_RADIANCE, ray, payload);
        rng = payload.rngState;

        radiance += throughput * payload.emitted;

        if (any(payload.direct > 0.0f) && IsSunVisible(payload.origin, rng))
            radiance += throughput * payload.direct;

        throughput *= payload.weight;

        if (all(throughput == 0.0f))
            break;

        // Russian roulette from the third segment on.
        if (segment >= 2)
        {
            float survival = saturate(max(throughput.r, max(throughput.g, throughput.b)));

            if (Random(rng) >= survival)
                break;

            throughput /= survival;
        }

        ray.Origin    = payload.origin;
        ray.Direction = payload.direction;
    }

    return radiance;
}

[shader("raygeneration")]
void RayGen()
{
    uint2  pixel = DispatchRaysIndex().xy;
    uint   rng   = PcgHash(PcgHash(pixel.x + PcgHash(pixel.y)) + Frame.frameIndex);
    float3 color = TracePath(CameraRay(float2(pixel) + Frame.jitter), rng);

    // Running average: frame n gets weight 1 / (n + 1).
    if (Frame.accumulatedFrames > 0)
        color = lerp(Accumulation[pixel].rgb, color, 1.0f / (Frame.accumulatedFrames + 1));

    Accumulation[pixel] = float4(color, 1.0f);
    Output[pixel]       = float4(PbrNeutral(color * kExposure), 1.0f);
}

[shader("miss")]
void MissRadiance(inout RadiancePayload payload)
{
    float t = smoothstep(0.0f, 0.5f, WorldRayDirection().y);

    payload.emitted = lerp(kSkyHorizon, kSkyZenith, t);
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

    // Lambert BRDF is albedo / pi, and kSunColor is already divided by pi.
    payload.direct = kAlbedo * kSunColor * saturate(dot(hit.shadingNormal, kSunDirection));
    payload.origin = OffsetRayOrigin(hit.smoothPosition, hit.geometricNormal);

    // Cosine-weighted bounce: the pdf cos / pi cancels the cosine and the 1 / pi of the BRDF, leaving the albedo.
    float3 direction = SampleCosineHemisphere(hit.shadingNormal, payload.rngState);

    // A direction around the shading normal can point into the triangle itself; mirror it back out.
    if (dot(direction, hit.geometricNormal) <= 0.0f)
        direction = reflect(direction, hit.geometricNormal);

    payload.weight    = kAlbedo;
    payload.direction = direction;
}
