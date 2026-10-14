#pragma once
#include <babylon_lite.h>
#include <functional>
#include <string>

namespace Scene35Host
{
    void Check(bl_Status status);
    bl_Runtime* Runtime();
    bl_EngineContext CreateEngine();
    std::string AssetPath(const std::string& name);
    void Run(bl_EngineContext engine);
}
