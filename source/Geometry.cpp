#include "Geometry.h"

#include "MathUtils.h"

namespace
{
	shared::Vertex MakeVertex(const Vec3& p, const Vec3& n, float u, float v)
	{
		return { {p.x, p.y, p.z}, {n.x, n.y, n.z}, {u, v} };
	}
}

MeshData CreateQuad()
{
	MeshData mesh;

	mesh.vertices = {
		{{-1, 0,  1}, {0, 1, 0}, {0, 1}},
		{{ 1, 0,  1}, {0, 1, 0}, {1, 1}},
		{{ 1, 0, -1}, {0, 1, 0}, {1, 0}},
		{{-1, 0, -1}, {0, 1, 0}, {0, 0}},
	};

	mesh.indices = { 0, 1, 2, 0, 2, 3 };

	return mesh;
}

MeshData CreateCube()
{
	MeshData mesh;

	mesh.vertices = {
		// +X
		{{ 1, -1,  1}, { 1,  0,  0}, {0, 1}},
		{{ 1, -1, -1}, { 1,  0,  0}, {1, 1}},
		{{ 1,  1, -1}, { 1,  0,  0}, {1, 0}},
		{{ 1,  1,  1}, { 1,  0,  0}, {0, 0}},
		// -X
		{{-1, -1, -1}, {-1,  0,  0}, {0, 1}},
		{{-1, -1,  1}, {-1,  0,  0}, {1, 1}},
		{{-1,  1,  1}, {-1,  0,  0}, {1, 0}},
		{{-1,  1, -1}, {-1,  0,  0}, {0, 0}},
		// +Y
		{{-1,  1,  1}, { 0,  1,  0}, {0, 1}},
		{{ 1,  1,  1}, { 0,  1,  0}, {1, 1}},
		{{ 1,  1, -1}, { 0,  1,  0}, {1, 0}},
		{{-1,  1, -1}, { 0,  1,  0}, {0, 0}},
		// -Y
		{{-1, -1, -1}, { 0, -1,  0}, {0, 1}},
		{{ 1, -1, -1}, { 0, -1,  0}, {1, 1}},
		{{ 1, -1,  1}, { 0, -1,  0}, {1, 0}},
		{{-1, -1,  1}, { 0, -1,  0}, {0, 0}},
		// +Z
		{{-1, -1,  1}, { 0,  0,  1}, {0, 1}},
		{{ 1, -1,  1}, { 0,  0,  1}, {1, 1}},
		{{ 1,  1,  1}, { 0,  0,  1}, {1, 0}},
		{{-1,  1,  1}, { 0,  0,  1}, {0, 0}},
		// -Z
		{{ 1, -1, -1}, { 0,  0, -1}, {0, 1}},
		{{-1, -1, -1}, { 0,  0, -1}, {1, 1}},
		{{-1,  1, -1}, { 0,  0, -1}, {1, 0}},
		{{ 1,  1, -1}, { 0,  0, -1}, {0, 0}},
	};

	mesh.indices = {
		 0,  1,  2,  0,  2,  3, // +X
		 4,  5,  6,  4,  6,  7, // -X
		 8,  9, 10,  8, 10, 11, // +Y
		12, 13, 14, 12, 14, 15, // -Y
		16, 17, 18, 16, 18, 19, // +Z
		20, 21, 22, 20, 22, 23, // -Z
	};

	return mesh;
}

MeshData CreateUvSphere(uint32_t slices, uint32_t stacks)
{
	MeshData mesh;

	for (uint32_t stack = 0; stack <= stacks; ++stack)
	{
		float v = static_cast<float>(stack) / stacks;
		float theta = v * kPi;
		for (uint32_t slice = 0; slice <= slices; ++slice)
		{
			float u = static_cast<float>(slice) / slices;
			float phi = u * 2.0f * kPi;

			Vec3 n(std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi));

			mesh.vertices.push_back(MakeVertex(n, n, u, v));
		}
	}

	for (uint32_t stack = 0; stack < stacks; ++stack)
	{
		for (uint32_t slice = 0; slice < slices; ++slice)
		{
			uint32_t a = stack * (slices + 1) + slice;
			uint32_t b = a + slices + 1;

			if (stack != 0)
				mesh.indices.insert(mesh.indices.end(), { a, b, a + 1 });

			if (stack != stacks - 1)
				mesh.indices.insert(mesh.indices.end(), { a + 1, b, b + 1 });
		}
	}

	return mesh;
}

MeshData CreateTorus(float majorRadius, float minorRadius, uint32_t majorSegments, uint32_t minorSegments)
{
	MeshData mesh;

	for (uint32_t i = 0; i <= majorSegments; ++i)
	{
		float u = static_cast<float>(i) / majorSegments;
		float phi = u * 2.0f * kPi;

		Vec3 ringCenter(std::cos(phi) * majorRadius, 0.0f, std::sin(phi) * majorRadius);
		Vec3 outward(std::cos(phi), 0.0f, std::sin(phi));

		for (uint32_t j = 0; j <= minorSegments; ++j)
		{
			float v = static_cast<float>(j) / minorSegments;
			float theta = v * 2.0f * kPi;

			Vec3 n = outward * std::cos(theta) + Vec3(0.0f, std::sin(theta), 0.0f);

			mesh.vertices.push_back(MakeVertex(ringCenter + n * minorRadius, n, u, v));
		}
	}

	for (uint32_t i = 0; i < majorSegments; ++i)
	{
		for (uint32_t j = 0; j < minorSegments; ++j)
		{
			uint32_t a = i * (minorSegments + 1) + j;
			uint32_t b = a + minorSegments + 1;

			mesh.indices.insert(mesh.indices.end(), { a, a + 1, b, a + 1, b + 1, b });
		}
	}

	return mesh;
}
