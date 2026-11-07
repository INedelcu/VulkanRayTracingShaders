#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#include "SharedTypes.h"

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

static const float  kPi           = 3.14159265f;
static const float3 kSunDirection = normalize(float3(-0.45f, 0.75f, 0.42f));
static const float3 kSunColor     = float3(2.2f, 2.1f, 1.9f); // irradiance / pi: what a white surface facing the sun reflects

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

#endif // COMMON_HLSLI
