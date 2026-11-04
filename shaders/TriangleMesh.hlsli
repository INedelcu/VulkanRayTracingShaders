#ifndef TRIANGLE_MESH_HLSLI
#define TRIANGLE_MESH_HLSLI

#include "SharedTypes.h"

StructuredBuffer<Vertex> Vertices;
StructuredBuffer<uint>   Indices;

struct SurfaceHit
{
    float3 position;
    float3 smoothPosition;
    float3 shadingNormal;
    float3 geometricNormal;
    float2 uv;
};

// "Hacking the Shadow Terminator", Ray Tracing Gems II 2021: projects p onto each vertex's tangent plane
// it lies below, then blends the projections with the barycentrics.
float3 LiftToSmoothSurface(float3 p, float3 p0, float3 p1, float3 p2, float3 n0, float3 n1, float3 n2, float3 barycentrics)
{
    float3 lift0 = max(0.0f, dot(p0 - p, n0)) * n0;
    float3 lift1 = max(0.0f, dot(p1 - p, n1)) * n1;
    float3 lift2 = max(0.0f, dot(p2 - p, n2)) * n2;

    return p + lift0 * barycentrics.x + lift1 * barycentrics.y + lift2 * barycentrics.z;
}

SurfaceHit FetchSurface(BuiltInTriangleIntersectionAttributes attributes)
{
    uint   baseIndex = PrimitiveIndex() * 3;
    Vertex v0 = Vertices[Indices[baseIndex + 0]];
    Vertex v1 = Vertices[Indices[baseIndex + 1]];
    Vertex v2 = Vertices[Indices[baseIndex + 2]];

    float3 barycentrics = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);

    float3x4 objectToWorld = ObjectToWorld3x4();
    float3 p0 = mul(objectToWorld, float4(v0.position, 1.0f));
    float3 p1 = mul(objectToWorld, float4(v1.position, 1.0f));
    float3 p2 = mul(objectToWorld, float4(v2.position, 1.0f));

    float3x3 worldToObject = (float3x3)WorldToObject3x4();
    float3 n0 = normalize(mul(v0.normal, worldToObject));
    float3 n1 = normalize(mul(v1.normal, worldToObject));
    float3 n2 = normalize(mul(v2.normal, worldToObject));

    float3 worldNormal = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
    float3 faceNormal  = normalize(cross(p1 - p0, p2 - p0));

    float3 rayDirection = WorldRayDirection();

    if (dot(faceNormal, rayDirection) > 0.0f)
        faceNormal = -faceNormal;

    if (dot(worldNormal, faceNormal) < 0.0f)
    {
        worldNormal = -worldNormal;
        n0 = -n0;
        n1 = -n1;
        n2 = -n2;
    }

    float3 position = WorldRayOrigin() + rayDirection * RayTCurrent();

    SurfaceHit hit;
    hit.position        = position;
    hit.smoothPosition  = LiftToSmoothSurface(position, p0, p1, p2, n0, n1, n2, barycentrics);
    hit.shadingNormal   = worldNormal;
    hit.geometricNormal = faceNormal;
    hit.uv              = v0.uv * barycentrics.x + v1.uv * barycentrics.y + v2.uv * barycentrics.z;

    return hit;
}

#endif // TRIANGLE_MESH_HLSLI
