#include "Common.hlsli"

RaytracingAccelerationStructure AccelStruct : register(t0, space1);
[[vk::image_format("rgba32f")]] RWTexture2D<float4> Accumulation : register(u1, space1);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> Output : register(u2, space1);
ConstantBuffer<FrameConstants> Frame : register(b0, space1);

static const float3 kSkyHorizon = float3(0.60f, 0.75f, 0.90f);
static const float3 kSkyZenith = float3(0.07, 0.21, 0.69);

static const float  kSunCosRadius    = cos(radians(2.0f)); // cosine of the sun disc's angular radius
static const uint   kMaxPathSegments = 8;
static const float  kExposure        = 0.8f;

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
