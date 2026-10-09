#pragma once
#include <babylon_lite.h>
#include <functional>

namespace Scene38Host
{
    void Check(bl_Status status);
    bl_Runtime* Runtime();
    bl_EngineContext CreateEngine();
    void Run(bl_EngineContext engine);
    void RetainCallback(std::function<void(double)> callback, bl_SceneContext scene);
}

namespace PrimitivesHost = Scene38Host;
