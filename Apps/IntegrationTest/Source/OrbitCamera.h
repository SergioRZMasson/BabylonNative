#pragma once

#include "MathTypes.h"
#include <algorithm>
#include <cmath>

namespace IntegrationTest
{

class OrbitCamera
{
public:
    float Distance = 3.0f;
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    Vector3 Target{0.0f, 0.0f, 0.0f};
    float FovDegrees = 45.0f;

    void FrameModel(const float center[3], const float extents[3])
    {
        Target = Vector3(center[0], center[1], center[2]);
        float maxExtent = std::max({extents[0], extents[1], extents[2]});
        if (maxExtent < 0.001f)
            maxExtent = 1.0f;

        // Set distance so the model fills the view
        float fovRad = FovDegrees * 3.14159265f / 180.0f;
        Distance = maxExtent / std::tan(fovRad * 0.5f) * 1.5f;
        Yaw = 0.0f;
        Pitch = 0.0f;
    }

    void Orbit(float deltaYaw, float deltaPitch)
    {
        Yaw += deltaYaw;
        Pitch += deltaPitch;
        constexpr float maxPitch = 1.5f;
        Pitch = std::clamp(Pitch, -maxPitch, maxPitch);
    }

    void Pan(float deltaX, float deltaY)
    {
        // Compute camera-local right and up vectors
        Vector3 right{std::cos(Yaw), 0.0f, -std::sin(Yaw)};
        Vector3 up{
            -std::sin(Pitch) * std::sin(Yaw),
            std::cos(Pitch),
            -std::sin(Pitch) * std::cos(Yaw)};

        float panScale = Distance * 0.002f;
        Target = Target + right * (deltaX * panScale) + up * (deltaY * panScale);
    }

    void Zoom(float delta)
    {
        Distance *= (1.0f - delta * 0.001f);
        Distance = std::max(Distance, 0.01f);
    }

    ICameraTransform GetCameraTransform() const
    {
        float posX = Target.x + Distance * std::cos(Pitch) * std::sin(Yaw);
        float posY = Target.y + Distance * std::sin(Pitch);
        float posZ = Target.z + Distance * std::cos(Pitch) * std::cos(Yaw);

        ICameraTransform cam;
        cam.SetPosition(Vector3(posX, posY, posZ));
        cam.SetTargetPoint(Target);
        cam.SetUpVector(Vector3(0.0f, 1.0f, 0.0f));
        cam.SetFovInDegree(FovDegrees);
        cam.SetNearClip(Distance * 0.01f);
        cam.SetFarClip(Distance * 100.0f);
        return cam;
    }
};

} // namespace IntegrationTest
