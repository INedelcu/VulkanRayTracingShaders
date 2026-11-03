#pragma once

#include "MathUtils.h"
#include "SharedTypes.h"

#include <cmath>
#include <cstdint>

struct OrbitCamera
{
    Vec3  target = { 0.0f, 1.0f, 0.0f };
    float yaw = 0.0f;
    float pitch = Radians(10.0f);
    float distance = 9.0f;
    float fovY = Radians(60.0f);

    shared::FrameConstants MakeFrameConstants(uint32_t width, uint32_t height) const
    {
        Vec3 offset(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
        Vec3 position = target + offset * distance;
        Vec3 forward = Normalize(target - position);
        Vec3 right = Normalize(Cross(forward, { 0.0f, 1.0f, 0.0f }));
        Vec3 up = Cross(right, forward);

        shared::FrameConstants frame = {};
        frame.cameraPosition = { position.x, position.y, position.z, std::tan(fovY * 0.5f) };
        frame.cameraRight = { right.x, right.y, right.z, (float)width / (float)height };
        frame.cameraUp = { up.x, up.y, up.z, 0.0f };
        frame.cameraForward = { forward.x, forward.y, forward.z, 0.0f };

        return frame;
    }
};
