#ifndef GGX_HLSLI
#define GGX_HLSLI

#include "Common.hlsli"

// GGX microfacets with height-correlated Smith masking-shadowing, sampled by their visible normals.
// alpha is the GGX width: the square of the perceptual roughness.

float RoughnessToAlpha(float roughness)
{
    // Kept above zero: a mirror's distribution is a spike the formulas below can't hold; 1e-4 is a mirror to the eye.
    return max(roughness * roughness, 1e-4f);
}

// Normal distribution ("Microfacet Models for Refraction through Rough Surfaces", EGSR 2007).
float DistributionGGX(float nDotH, float alpha)
{
    float a2 = alpha * alpha;
    float t  = nDotH * nDotH * (a2 - 1.0f) + 1.0f;

    return a2 / (kPi * t * t);
}

// Smith masking of one direction ("Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs", JCGT 2014).
float SmithG1(float nDotV, float alpha)
{
    float a2 = alpha * alpha;

    return 2.0f * nDotV / (nDotV + sqrt(a2 + (1.0f - a2) * nDotV * nDotV));
}

// Height-correlated Smith masking-shadowing of both directions (same paper).
float SmithG2(float nDotL, float nDotV, float alpha)
{
    float a2      = alpha * alpha;
    float lambdaV = nDotL * sqrt(a2 + (1.0f - a2) * nDotV * nDotV);
    float lambdaL = nDotV * sqrt(a2 + (1.0f - a2) * nDotL * nDotL);

    return 2.0f * nDotL * nDotV / (lambdaV + lambdaL);
}

// A microfacet normal visible from v, both in the frame where the macro normal is +z ("Sampling the GGX Distribution
// of Visible Normals", JCGT 2018): stretch v so the microfacets become a hemisphere, sample the disk it projects to,
// and unstretch.
float3 SampleVisibleNormal(float3 v, float alpha, inout uint rng)
{
    float3 vh    = normalize(float3(alpha * v.x, alpha * v.y, v.z));
    float  lenSq = vh.x * vh.x + vh.y * vh.y;
    float3 t1    = lenSq > 0.0f ? float3(-vh.y, vh.x, 0.0f) * rsqrt(lenSq) : float3(1.0f, 0.0f, 0.0f);
    float3 t2    = cross(vh, t1);

    float r   = sqrt(Random(rng));
    float phi = 2.0f * kPi * Random(rng);
    float p1  = r * cos(phi);
    float p2  = r * sin(phi);
    float s   = 0.5f * (1.0f + vh.z);
    p2 = (1.0f - s) * sqrt(1.0f - p1 * p1) + s * p2;

    float3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0f, 1.0f - p1 * p1 - p2 * p2)) * vh;

    return normalize(float3(alpha * nh.x, alpha * nh.y, max(0.0f, nh.z)));
}

// The same in world space: n is the macro normal and v points to the viewer, on n's side.
float3 SampleGgxHalfVector(float3 v, float3 n, float alpha, inout uint rng)
{
    float3 t, b;
    BuildOrthonormalBasis(n, t, b);

    float3 h = SampleVisibleNormal(float3(dot(v, t), dot(v, b), dot(v, n)), alpha, rng);

    return h.x * t + h.y * b + h.z * n;
}

// The specular BRDF times cos(theta_l), D G2 F / (4 n.v), and the density SampleSpecularGGX would draw l with.
float3 EvaluateSpecularGGX(float3 v, float3 l, float3 n, float3 f0, float alpha, out float pdf)
{
    float nDotV = dot(n, v);
    float nDotL = dot(n, l);

    pdf = 0.0f;

    if (nDotV <= 0.0f || nDotL <= 0.0f)
        return 0.0f;

    float3 h = normalize(v + l);
    float  d = DistributionGGX(saturate(dot(n, h)), alpha);

    pdf = SmithG1(nDotV, alpha) * d / (4.0f * nDotV);

    return FresnelSchlick(f0, dot(v, h)) * (d * SmithG2(nDotL, nDotV, alpha) / (4.0f * nDotV));
}

// Reflects v off a visible microfacet. The weight, BRDF times cos over pdf, collapses to F G2 / G1 (Heitz 2018, eq. 19).
// False when the microfacet sends l below the surface (in reality the light scatters again; single scattering drops
// it) or v grazes the surface.
bool SampleSpecularGGX(float3 v, float3 n, float3 f0, float alpha, inout uint rng, out float3 l, out float3 weight, out float pdf)
{
    float3 h     = SampleGgxHalfVector(v, n, alpha, rng);
    float  nDotV = dot(n, v);

    l      = reflect(-v, h);
    weight = 0.0f;
    pdf    = 0.0f;

    float nDotL = dot(n, l);

    if (nDotV <= 0.0f || nDotL <= 0.0f)
        return false;

    float g1 = SmithG1(nDotV, alpha);

    weight = FresnelSchlick(f0, dot(v, h)) * (SmithG2(nDotL, nDotV, alpha) / g1);
    pdf    = g1 * DistributionGGX(saturate(dot(n, h)), alpha) / (4.0f * nDotV);

    return true;
}

#endif // GGX_HLSLI
