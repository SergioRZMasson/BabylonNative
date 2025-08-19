#include "GraphicsDebug.h"

#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <DXGItype.h>
#include <DXProgrammableCapture.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#ifdef BABYLON_GRAPHICS_DEBUG

#define VISUAL_STUDIO_GRAPHICS_DEBUG

#ifdef VISUAL_STUDIO_GRAPHICS_DEBUG

namespace
{

    typedef HRESULT (*DXGIGetDebugInterface1FPtr)(UINT Flags, REFIID riid, _COM_Outptr_ void** pDebug);

    IDXGraphicsAnalysis* pGraphicsAnalysis = nullptr;

    static bool s_fSupported = true;

    // Set the number of frames to capture.
    unsigned int g_captureCount = 0;

} // namespace

void GraphicsDebug_Load()
{
    if (pGraphicsAnalysis != nullptr || !s_fSupported)
    {
        return;
    }

    HMODULE mod = GetModuleHandle("Dxgi.dll");

    if (mod == nullptr)
    {
        s_fSupported = false;
        return;
    }

    DXGIGetDebugInterface1FPtr DXGIGetDebugInterface1Ptr = (DXGIGetDebugInterface1FPtr)GetProcAddress(mod, "DXGIGetDebugInterface1");

    if (DXGIGetDebugInterface1Ptr == nullptr)
    {
        s_fSupported = false;
        return;
    }

    HRESULT getAnalysis = DXGIGetDebugInterface1Ptr(0, __uuidof(pGraphicsAnalysis), reinterpret_cast<void**>(&pGraphicsAnalysis));

    if (FAILED(getAnalysis))
    {
        OutputDebugStringA("Unable to start IDXGraphicsAnalysis\n");
        s_fSupported = false;
    }
}

void GraphicsDebug_BeginFrameCapture(Microsoft::WRL::ComPtr<ID3D11Device>) noexcept
{
    if (g_captureCount == 0)
    {
        return;
    }

    if (pGraphicsAnalysis)
    {
        pGraphicsAnalysis->BeginCapture();
    }
}

void GraphicsDebug_EndFrameCapture(Microsoft::WRL::ComPtr<ID3D11Device>) noexcept
{
    if (g_captureCount == 0)
    {
        return;
    }

    if (pGraphicsAnalysis)
    {
        pGraphicsAnalysis->EndCapture();
    }

    --g_captureCount;
}

#else

#include "C:\\Program Files\\RenderDoc\\renderdoc_app.h"
#include <cassert>

namespace
{
    RENDERDOC_API_1_1_2* rdoc_api = nullptr;
}

void GraphicsDebug_Load()
{
    if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&rdoc_api);
        assert(ret == 1);
    }
}

void GraphicsDebug_BeginFrameCapture(Microsoft::WRL::ComPtr<ID3D11Device> device) noexcept
{
    if (rdoc_api)
    {
        rdoc_api->StartFrameCapture(device.Get(), nullptr);
    }
}

void GraphicsDebug_EndFrameCapture(Microsoft::WRL::ComPtr<ID3D11Device> device) noexcept
{
    if (rdoc_api)
    {
        rdoc_api->EndFrameCapture(device.Get(), nullptr);
    }
}

#endif

#endif