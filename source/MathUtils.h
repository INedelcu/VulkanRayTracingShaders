#pragma once

#include <cmath>
#include <cstdint>

struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3  operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3  operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3  operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3  operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};

inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3  Cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
inline Vec3  Normalize(const Vec3& v) { return v * (1.0f / Length(v)); }

constexpr float kPi = 3.14159265358979f;
inline float Radians(float degrees) { return degrees * (kPi / 180.0f); }

// Radical inverse: the digits of index in the given base, mirrored around the decimal point.
inline float Halton(uint32_t index, uint32_t base)
{
    float result = 0.0f;
    float fraction = 1.0f;

    while (index > 0)
    {
        fraction /= base;
        result += fraction * (index % base);
        index /= base;
    }

    return result;
}

// Row-major 3x4 affine transform, the layout of VkTransformMatrixKHR.
struct Transform
{
    float m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};

    static Transform Translation(const Vec3& t)
    {
        Transform r;
        r.m[0][3] = t.x; r.m[1][3] = t.y; r.m[2][3] = t.z;
        return r;
    }

    static Transform Scale(const Vec3& s)
    {
        Transform r;
        r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
        return r;
    }

    static Transform RotationX(float radians)
    {
        Transform r;
        float c = std::cos(radians), s = std::sin(radians);
        r.m[1][1] = c; r.m[1][2] = -s;
        r.m[2][1] = s; r.m[2][2] = c;
        return r;
    }

    static Transform RotationY(float radians)
    {
        Transform r;
        float c = std::cos(radians), s = std::sin(radians);
        r.m[0][0] = c;  r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }

    // Composition: (a * b) applies b first, then a.
    friend Transform operator*(const Transform& a, const Transform& b)
    {
        Transform r;
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                float v = a.m[row][0] * b.m[0][col] + a.m[row][1] * b.m[1][col] + a.m[row][2] * b.m[2][col];
                if (col == 3)
                    v += a.m[row][3];
                r.m[row][col] = v;
            }
        }
        return r;
    }
};
