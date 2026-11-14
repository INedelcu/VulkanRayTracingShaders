#include "Common.hlsli"
#include "TriangleMesh.hlsli"
#include "GGX.hlsli"

// What a diffuse material holds, filled in by name from the values its material sets on the host: the hit record of
// the instance holds these, and DXC's reflection of this block is what tells the host where each one goes.
struct DiffuseMaterial
{
    float3 color;     // albedo of the diffuse base
    float  ior;       // index of refraction of the coat, 1.5 reflects 4% of a head on view
    float  roughness; // of the coat: 1 is matte, 0 a mirror
};

ConstantBuffer<DiffuseMaterial> Material; // data in the instance's hit record

// A diffuse base under a dielectric coat, smooth or rough: PathTracing/Standard with metallic 0 in the Unity path
// tracer. Each bounce follows one of the two lobes, picked by how bright each is.
[shader("closesthit")]
void ClosestHitDiffuse(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    SurfaceHit hit   = FetchSurface(attributes);
    float3     v     = -WorldRayDirection();
    float      alpha = RoughnessToAlpha(Material.roughness);

    // The coat reflects f0 at normal incidence, from its index of refraction (4% at 1.5), and more toward grazing
    // angles. The light it doesn't reflect reaches the diffuse base.
    float  f0Root      = (Material.ior - 1.0f) / (Material.ior + 1.0f);
    float  f0          = f0Root * f0Root;
    float3 diffuseTint = Material.color * (1.0f - f0);

    // Interpolated normals can turn away from a grazing view.
    float3 n = dot(hit.shadingNormal, v) > 0.0f ? hit.shadingNormal : hit.geometricNormal;

    // The sun through a shadow ray toward a random point of its disc, for both lobes: bounces don't see the disc.
    // Each lobe reflects the sun's irradiance times its BRDF times cos. A coat sharper than the disc, or than the
    // roughest surface the path came through, is widened here (SunLobeAlpha), not in its bounces. Without multiple
    // importance sampling, the densities go unused.
    payload.sunDirection = SampleSunDirection(payload.rngState);

    float  coatSunPdf;
    float3 coatSun = EvaluateSpecularGGX(v, payload.sunDirection, n, f0, SunLobeAlpha(alpha, payload.pathAlpha), coatSunPdf);

    // After a diffuse bounce the coat's glint would light that surface as a caustic, found too rarely to converge
    // without fireflies: like metal and glass, leave it out on those paths.
    if ((payload.flags & kFlagSpecularPath) == 0)
        coatSun = 0.0f;

    payload.direct = (diffuseTint / kPi * saturate(dot(hit.shadingNormal, payload.sunDirection)) + coatSun) * kSunIrradiance;
    payload.origin = OffsetRayOrigin(hit.smoothPosition, hit.geometricNormal);

    // The coat's share of the bounces, from the brightness of each lobe, kept within [0.1, 0.9] so neither starves.
    // Each lobe's weight is divided by the chance of picking it.
    float coatChance = clamp(f0 / max(f0 + Luminance(Material.color), 1e-7f), 0.1f, 0.9f);

    if (Random(payload.rngState) < coatChance)
    {
        // Reflect off a visible microfacet of the coat: the weight collapses to F G2 / G1 (Heitz 2018, eq. 19).
        // Like metal, it stays the kind of path it was. A microfacet can send the ray below the surface, where it
        // would scatter again; single scattering drops it.
        float3 direction;
        float3 coatWeight;
        float  coatPdf;

        if (!SampleSpecularGGX(v, n, f0, alpha, payload.rngState, direction, coatWeight, coatPdf))
        {
            payload.weight = 0.0f;
            return;
        }

        payload.weight = coatWeight / coatChance;

        // Interpolated normals can still bend the ray into the triangle; keep it above.
        if (dot(direction, hit.geometricNormal) <= 0.0f)
            direction = reflect(direction, hit.geometricNormal);

        payload.direction = direction;
        payload.pathAlpha = max(payload.pathAlpha, alpha);
        return;
    }

    // Cosine-weighted bounce: the pdf cos / pi cancels the cosine and the 1 / pi of the BRDF, leaving the albedo.
    float3 direction = SampleCosineHemisphere(hit.shadingNormal, payload.rngState);

    // A direction around the shading normal can point into the triangle itself; mirror it back out.
    if (dot(direction, hit.geometricNormal) <= 0.0f)
        direction = reflect(direction, hit.geometricNormal);

    payload.weight    = diffuseTint / (1.0f - coatChance);
    payload.direction = direction;
    payload.flags     = 0;
    payload.pathAlpha = 1.0f;
}
