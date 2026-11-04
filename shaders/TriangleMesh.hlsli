#ifndef TRIANGLE_MESH_HLSLI
#define TRIANGLE_MESH_HLSLI

#include "SharedTypes.h"

StructuredBuffer<Vertex> Vertices;
StructuredBuffer<uint>   Indices;

struct SurfaceHit
{
    float3 position;
    float3 shadingNormal;
    float3 geometricNormal;
    float2 uv;
};

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

    float3 objectNormal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    float3 worldNormal  = normalize(mul(objectNormal, (float3x3)WorldToObject3x4()));
    float3 faceNormal   = normalize(cross(p1 - p0, p2 - p0));

    float3 rayDirection = WorldRayDirection();

    if (dot(faceNormal, rayDirection) > 0.0f)
        faceNormal = -faceNormal;

    if (dot(worldNormal, faceNormal) < 0.0f)
        worldNormal = -worldNormal;

    SurfaceHit hit;
    hit.position        = WorldRayOrigin() + rayDirection * RayTCurrent();
    hit.shadingNormal   = worldNormal;
    hit.geometricNormal = faceNormal;
    hit.uv              = v0.uv * barycentrics.x + v1.uv * barycentrics.y + v2.uv * barycentrics.z;

    return hit;
}

#endif // TRIANGLE_MESH_HLSLI
