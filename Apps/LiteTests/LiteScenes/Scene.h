#pragma once

#include <babylon_lite.h>

namespace LiteScenes
{
    bool CreateM1Scene(uint32_t id, bl_Runtime* runtime, bl_EngineContext engine,
                       bl_SceneContext scene);
    void Check(bl_Status status);
    bl_String String(const char* text);
    bl_SceneContext CreateScene(uint32_t id, bl_Runtime* runtime, bl_EngineContext engine);
}
