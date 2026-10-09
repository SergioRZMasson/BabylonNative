#pragma once
#include <babylon_lite.h>
#include <functional>
#include <vector>
#include <string>

namespace CubeHost
{
    void Check(bl_Status status);
    bl_Runtime* Runtime();
    bl_EngineContext CreateEngine();
    void Run(bl_EngineContext engine);
    void RetainCallback(std::function<void(double)> callback, bl_SceneContext scene);
}
