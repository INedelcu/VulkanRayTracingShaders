#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#include "SharedTypes.h"

// Set on camera rays and kept through mirror bounces: these rays can see the sun disc.
static const uint kFlagSpecularPath = 1;

struct RadiancePayload
{
    float3 emitted;         // light reaching the ray origin without a shadow test: the sky
    float3 direct;          // sun light leaving the hit, added only if the sun is visible from origin along sunDirection
    float3 sunDirection;    // toward a random point of the sun disc: the shadow ray
    float3 weight;          // throughput factor of the bounce ray; zero ends the path
    float3 origin;          // hit position moved off the surface, start of the shadow and bounce rays
    float3 direction;       // bounce ray direction
    uint   rngState;
    uint   flags;           // kFlag*: in for this ray, out for the bounce ray
    float  pathAlpha;       // widest GGX alpha the path has bounced off, 1 after diffuse: glossy sun terms widen to it, so a
                            // glint seen through frosted glass is a soft glow instead of sparkles ("Path Space Regularization
                            // for Holistic and Robust Light Transport", Kaplanyan and Dachsbacher 2013). In for this ray, out
                            // for the bounce ray.
};

struct ShadowPayload
{
    float  visibility;    // 1 when the ray reached the sky, 0 when an opaque surface stopped it
    float3 transmittance; // what the glass the ray crossed lets through
};

static const float  kPi            = 3.14159265f;
static const float3 kSunDirection  = normalize(float3(-0.45f, 0.75f, 0.42f));
static const float3 kSunColor      = float3(2.2f, 2.1f, 1.9f); // what a white diffuse surface facing the sun reflects
static const float3 kSunIrradiance = kSunColor * kPi; // the light falling on a surface facing the sun
static const float  kSunCosRadius  = cos(radians(2.0f)); // cosine of the sun disc's angular radius

// The GGX width whose microfacets spread a reflection about as wide as the sun disc: half the disc's angular radius,
// since a tilted microfacet turns the reflection by twice its tilt. Glossy lobes narrower than this are widened to it
// for the sun's shadow-ray term only. A narrower lobe would light up only when the random point on the disc falls inside
// it, rarely enough that the glint shows as stray sparkles for minutes; widened, it is a disc-sized glint of about the
// same energy.
static const float kSunAlpha = 0.5f * sqrt(1.0f - kSunCosRadius * kSunCosRadius);

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

// Rec. 709 luminance.
float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// Schlick's approximation of Fresnel reflectance; f0 is the reflectance at normal incidence.
float3 FresnelSchlick(float3 f0, float cosTheta)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

#endif // COMMON_HLSLI
