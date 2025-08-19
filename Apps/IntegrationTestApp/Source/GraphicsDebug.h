#pragma once

#include "RenderTypes.h"

#ifdef _DEBUG
    #ifndef BABYLON_GRAPHICS_DEBUG
        #define BABYLON_GRAPHICS_DEBUG 1
    #endif
#endif

#ifdef BABYLON_GRAPHICS_DEBUG
    void GraphicsDebug_Load();
    void GraphicsDebug_BeginFrameCapture(BabylonRendererDevicePtr) noexcept;
    void GraphicsDebug_EndFrameCapture(BabylonRendererDevicePtr) noexcept;

    #define BABYLON_GRAPHICS_DEBUG_INIT()                       GraphicsDebug_Load()
    #define BABYLON_GRAPHICS_DEBUG_BEGIN_FRAME_CAPTURE(Device)  GraphicsDebug_BeginFrameCapture(Device)
    #define BABYLON_GRAPHICS_DEBUG_END_FRAME_CAPTURE(Device)    GraphicsDebug_EndFrameCapture(Device)
#else
    #define BABYLON_GRAPHICS_DEBUG_INIT()
    #define BABYLON_GRAPHICS_DEBUG_BEGIN_FRAME_CAPTURE()
    #define BABYLON_GRAPHICS_DEBUG_END_FRAME_CAPTURE()
#endif