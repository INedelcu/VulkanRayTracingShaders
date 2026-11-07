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

uint3 FetchTriangleIndices(uint primitiveIndex)
{
    uint baseIndex = primitiveIndex * 3;

    return uint3(Indices[baseIndex + 0], Indices[baseIndex + 1], Indices[baseIndex + 2]);
}

Vertex FetchVertex(uint vertexIndex)
{
    return Vertices[vertexIndex];
}

// Reconstructs the hit position from the triangle vertices in object space, keeping the barycentric interpolation in
// a precise edge based mad chain ("Solving Self-Intersection Artifacts in DirectX Raytracing", van Antwerpen 2023).
// Building the result from the two edges and adding the base vertex last avoids the cancellation of the plain weighted
// sum of the three vertices, which perturbs the hit point and feeds self intersection. Feed the result through
// TransformObjectToWorldPositionPrecise.
float3 InterpolatePositionPrecise(float3 p0, float3 p1, float3 p2, float3 barycentrics)
{
    precise float3 edge0    = p1 - p0;
    precise float3 edge1    = p2 - p0;
    precise float3 position = p0 + mad(barycentrics.y, edge0, mul(barycentrics.z, edge1));

    return position;
}

// Transforms an object space position to world space while preserving precision in the result (same paper). The
// translation column is added last as a separate term so it does not swamp the low order bits of the rotated and
// scaled position, and expanding the matrix multiply by hand keeps the rest of the work in a precise mad chain instead
// of the default mul. It complements the precise barycentric reconstruction of the object space position, keeping
// world space hit points stable far from the origin.
float3 TransformObjectToWorldPositionPrecise(float3 objectPosition)
{
    const float3x4 o2w = ObjectToWorld3x4();

    precise float3 worldPosition;
    worldPosition.x = o2w._m03 + mad(o2w._m00, objectPosition.x, mad(o2w._m01, objectPosition.y, mul(o2w._m02, objectPosition.z)));
    worldPosition.y = o2w._m13 + mad(o2w._m10, objectPosition.x, mad(o2w._m11, objectPosition.y, mul(o2w._m12, objectPosition.z)));
    worldPosition.z = o2w._m23 + mad(o2w._m20, objectPosition.x, mad(o2w._m21, objectPosition.y, mul(o2w._m22, objectPosition.z)));

    return worldPosition;
}

// A vertex attribute at the barycentrics. A template rather than a macro: dxc gives a macro's expanded columns to the
// line that uses it, which the embedded-source debug info of Debug builds rejects as invalid SPIR-V.
template<typename T>
T InterpolateAttribute(T a0, T a1, T a2, float3 barycentrics)
{
    return a0 * barycentrics.x + a1 * barycentrics.y + a2 * barycentrics.z;
}

// The vertex at the barycentrics: the position precisely, the other attributes as plain weighted sums. The normal is
// left unnormalized.
Vertex InterpolateVertices(Vertex v0, Vertex v1, Vertex v2, float3 barycentrics)
{
    Vertex v;
    v.position = InterpolatePositionPrecise(v0.position, v1.position, v2.position, barycentrics);
    v.normal   = InterpolateAttribute(v0.normal, v1.normal, v2.normal, barycentrics);
    v.uv       = InterpolateAttribute(v0.uv, v1.uv, v2.uv, barycentrics);

    return v;
}

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
    uint3  triangleIndices = FetchTriangleIndices(PrimitiveIndex());
    Vertex v0 = FetchVertex(triangleIndices.x);
    Vertex v1 = FetchVertex(triangleIndices.y);
    Vertex v2 = FetchVertex(triangleIndices.z);

    float3 barycentrics = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);
    Vertex v = InterpolateVertices(v0, v1, v2, barycentrics);

    // The corners in world space, for the face normal and for lifting the hit point onto the smooth surface.
    float3 p0 = TransformObjectToWorldPositionPrecise(v0.position);
    float3 p1 = TransformObjectToWorldPositionPrecise(v1.position);
    float3 p2 = TransformObjectToWorldPositionPrecise(v2.position);

    // Normals transform with the inverse transpose of the object-to-world matrix, to stay perpendicular to the surface
    // under non-uniform scale.
    float3x3 worldToObject = (float3x3)WorldToObject3x4();
    float3 n0 = normalize(mul(v0.normal, worldToObject));
    float3 n1 = normalize(mul(v1.normal, worldToObject));
    float3 n2 = normalize(mul(v2.normal, worldToObject));

    float3 worldNormal = normalize(mul(v.normal, worldToObject));
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

    // On the triangle, rebuilt from its vertices rather than travelled along the ray: origin + t * direction loses the
    // low bits of the position as t grows.
    float3 position = TransformObjectToWorldPositionPrecise(v.position);

    SurfaceHit hit;
    hit.position        = position;
    hit.smoothPosition  = LiftToSmoothSurface(position, p0, p1, p2, n0, n1, n2, barycentrics);
    hit.shadingNormal   = worldNormal;
    hit.geometricNormal = faceNormal;
    hit.uv              = v.uv;

    return hit;
}

#endif // TRIANGLE_MESH_HLSLI
