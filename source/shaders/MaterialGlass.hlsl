#include "Common.hlsli"
#include "TriangleMesh.hlsli"
#include "GGX.hlsli"

// What a glass material holds, filled in by name from the values its material sets on the host.
struct GlassMaterial
{
    float3 color;      // what crossing a unit of it leaves of each channel
    float  ior;        // index of refraction
    float  extinction; // tint per unit crossed: 0 = clear, 1 = a color channel at 0 keeps 37%, 3 = keeps 5%
    float  roughness;  // 1 is matte, 0 a mirror
};

ConstantBuffer<GlassMaterial> Material; // data in the instance's hit record

// Fresnel reflectance of a smooth dielectric for unpolarized light; eta is the incident over the transmitted index of
// refraction. Past the critical angle all light reflects: total internal reflection.
float FresnelDielectric(float cosThetaI, float eta)
{
    float sinThetaTSq = eta * eta * (1.0f - cosThetaI * cosThetaI);

    if (sinThetaTSq >= 1.0f)
        return 1.0f;

    float cosThetaT = sqrt(1.0f - sinThetaTSq);
    float rs        = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    float rp        = (cosThetaI - eta * cosThetaT) / (cosThetaI + eta * cosThetaT);

    return 0.5f * (rs * rs + rp * rp);
}

[shader("closesthit")]
void ClosestHitGlass(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    SurfaceHit hit   = FetchSurface(attributes);
    float3     v     = -WorldRayDirection();
    float      eta   = hit.frontFace ? 1.0f / Material.ior : Material.ior;
    float      alpha = RoughnessToAlpha(Material.roughness);

    // Interpolated normals can turn away from a grazing view.
    float3 n = dot(hit.shadingNormal, v) > 0.0f ? hit.shadingNormal : hit.geometricNormal;

    // A visible microfacet reflects with its Fresnel probability and refracts otherwise. The probability cancels the
    // Fresnel factor, so the weight is G2 / G1 in both branches (Walter 2007, Heitz 2018); a smooth surface gives 1.
    float3 h         = SampleGgxHalfVector(v, n, alpha, payload.rngState);
    bool   reflected = Random(payload.rngState) < FresnelDielectric(dot(v, h), eta);
    float3 direction = reflected ? reflect(-v, h) : refract(-v, h, eta);
    float  nDotL     = dot(n, direction);
    float  nDotV     = dot(n, v);

    // A reflection below the surface or a refraction above it would scatter again; single scattering drops it.
    if (nDotV <= 0.0f || (reflected ? nDotL <= 0.0f : nDotL >= 0.0f))
    {
        payload.weight = 0.0f;
        return;
    }

    float3 absorption = hit.frontFace ? 1.0f : exp(-(1.0f - Material.color) * Material.extinction * RayTCurrent());

    payload.weight = absorption * (SmithG2(abs(nDotL), nDotV, alpha) / SmithG1(nDotV, alpha));
    payload.origin    = OffsetRayOrigin(hit.position, dot(direction, hit.geometricNormal) > 0.0f ? hit.geometricNormal : -hit.geometricNormal);
    payload.direction = direction;
    payload.pathAlpha = max(payload.pathAlpha, alpha);
}

// Shadow rays cross glass in a straight line instead of refracting, so the shadow takes the glass's color but the
// light is not focused into a caustic. Each crossing keeps what the interface doesn't reflect, and the inside absorbs
// by Beer-Lambert over the length between entering and leaving.
[shader("anyhit")]
void AnyHitGlassShadow(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    SurfaceHit hit            = FetchSurface(attributes);
    float3     rayDirection   = WorldRayDirection();
    float      eta            = hit.frontFace ? 1.0f / Material.ior : Material.ior;
    float3     absorptionRate = (1.0f - Material.color) * Material.extinction;

    // Hits arrive in any order: entering multiplies by exp(+a t) and leaving by exp(-a t), so the product is
    // exp(-a (t_leave - t_enter)) however they come. The geometry's no-duplicate flag keeps each crossing to one call.
    float lengthSign = hit.frontFace ? 1.0f : -1.0f;

    payload.transmittance *= (1.0f - FresnelDielectric(abs(dot(hit.shadingNormal, rayDirection)), eta)) * exp(lengthSign * absorptionRate * RayTCurrent());

    IgnoreHit();
}
