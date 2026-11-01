#pragma once

#include "SharedTypes.h"

#include <cstdint>
#include <vector>

struct MeshData
{
    std::vector<shared::Vertex> vertices;
    std::vector<uint32_t>       indices;
};

// Geometry in XZ plane, [-1, 1] x [-1, 1], normal (0, 1, 0)
MeshData CreateQuad();

// [-1, 1] x [-1, 1] x [-1, 1]
MeshData CreateCube();

// Sphere with radius 1
MeshData CreateUvSphere(uint32_t slices, uint32_t stacks);

// XZ plane torus
MeshData CreateTorus(float majorRadius, float minorRadius, uint32_t majorSegments, uint32_t minorSegments);
