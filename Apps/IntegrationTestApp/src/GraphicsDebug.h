#pragma once

#include <d3d11.h>

// Debug defaults

#ifdef _DEBUG
#ifndef BABYLON_GRAPHICS_DEBUG
#define BABYLON_GRAPHICS_DEBUG 1
#endif
#ifndef BABYLON_PRINT_DEBUG
#define BABYLON_PRINT_DEBUG 1
#endif
#endif

// Graphics debug

#ifdef BABYLON_GRAPHICS_DEBUG

void GraphicsDebug_Load();
void GraphicsDebug_BeginFrameCapture(ID3D11Device*) noexcept;
void GraphicsDebug_EndFrameCapture(ID3D11Device*) noexcept;

#define BABYLON_GRAPHICS_DEBUG_INIT()                GraphicsDebug_Load()
#define BABYLON_GRAPHICS_DEBUG_BEGIN_FRAME_CAPTURE(Device) GraphicsDebug_BeginFrameCapture(Device)
#define BABYLON_GRAPHICS_DEBUG_END_FRAME_CAPTURE(Device)   GraphicsDebug_EndFrameCapture(Device)

#else

#define BABYLON_GRAPHICS_DEBUG_INIT()
#define BABYLON_GRAPHICS_DEBUG_BEGIN_FRAME_CAPTURE()
#define BABYLON_GRAPHICS_DEBUG_END_FRAME_CAPTURE()

#endif