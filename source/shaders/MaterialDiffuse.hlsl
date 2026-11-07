#include "Common.hlsli"
#include "TriangleMesh.hlsli"

ConstantBuffer<MaterialConstants> Material; // data in the instance's hit record

[shader("closesthit")]
void ClosestHitDiffuse(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    SurfaceHit hit    = FetchSurface(attributes);
    float3     albedo = Material.color.rgb;

    // Lambert BRDF is albedo / pi, and kSunColor is already divided by pi.
    payload.direct = albedo * kSunColor * saturate(dot(hit.shadingNormal, kSunDirection));
    payload.origin = OffsetRayOrigin(hit.smoothPosition, hit.geometricNormal);

    // Cosine-weighted bounce: the pdf cos / pi cancels the cosine and the 1 / pi of the BRDF, leaving the albedo.
    float3 direction = SampleCosineHemisphere(hit.shadingNormal, payload.rngState);

    // A direction around the shading normal can point into the triangle itself; mirror it back out.
    if (dot(direction, hit.geometricNormal) <= 0.0f)
        direction = reflect(direction, hit.geometricNormal);

    payload.weight    = albedo;
    payload.direction = direction;
}
