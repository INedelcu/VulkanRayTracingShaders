#include "Common.hlsli"
#include "TriangleMesh.hlsli"
#include "GGX.hlsli"

ConstantBuffer<MaterialConstants> Material; // data in the instance's hit record

[shader("closesthit")]
void ClosestHitMetal(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    SurfaceHit hit   = FetchSurface(attributes);
    float3     v     = -WorldRayDirection();
    float      alpha = RoughnessToAlpha(Material.roughness);

    // Interpolated normals can turn away from a grazing view.
    float3 n = dot(hit.shadingNormal, v) > 0.0f ? hit.shadingNormal : hit.geometricNormal;

    // The sun through a shadow ray toward a random point of its disc: bounces don't see the disc. The metal reflects
    // the sun's irradiance times its BRDF times cos. A lobe sharper than the disc, or than the roughest surface the path
    // came through, is widened here (SunLobeAlpha), not in its bounces. Without multiple importance sampling, the
    // density goes unused.
    payload.sunDirection = SampleSunDirection(payload.rngState);

    float  sunPdf;
    float3 sun = EvaluateSpecularGGX(v, payload.sunDirection, n, Material.color.rgb, SunLobeAlpha(alpha, payload.pathAlpha), sunPdf);

    // After a diffuse bounce the glint would light that surface as a caustic, found too rarely to converge without
    // fireflies: like the coat, leave it out on those paths.
    if ((payload.flags & kFlagSpecularPath) == 0)
        sun = 0.0f;

    payload.direct = sun * kSunIrradiance;
    payload.origin = OffsetRayOrigin(hit.smoothPosition, hit.geometricNormal);

    // Reflect off a visible microfacet. The weight, BRDF times cos over pdf, collapses to F G2 / G1 (Heitz 2018,
    // eq. 19); a smooth surface gives the mirror. Fresnel tints it with the metal's color, fading to white at grazing
    // angles. A microfacet can send the ray below the surface, where it would scatter again; single scattering drops it.
    float3 direction;
    float3 weight;
    float  pdf;

    if (!SampleSpecularGGX(v, n, Material.color.rgb, alpha, payload.rngState, direction, weight, pdf))
    {
        payload.weight = 0.0f;
        return;
    }

    // The reflection stays a specular path.
    payload.weight = weight;

    // Interpolated normals can still bend the ray into the triangle; keep it above.
    if (dot(direction, hit.geometricNormal) <= 0.0f)
        direction = reflect(direction, hit.geometricNormal);

    payload.direction = direction;
    payload.pathAlpha = max(payload.pathAlpha, alpha);
}
