#pragma once

// RenderDoc frame capture integration.
// Brackets GPU work in Render() so RenderDoc captures the full Babylon render stack.
// Enabled only when INTEGRATION_TEST_RENDERDOC is defined.

#ifdef INTEGRATION_TEST_RENDERDOC

#include <renderdoc_app.h>
#include <Windows.h>

namespace IntegrationTest
{

class RenderDocCapture
{
public:
    RenderDocCapture()
    {
        if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI =
                reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
            if (RENDERDOC_GetAPI)
            {
                RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&m_api));
            }
        }
    }

    bool IsAvailable() const { return m_api != nullptr; }

    // Call before the GPU work you want to capture.
    void BeginFrameCapture(void* device = nullptr, void* window = nullptr)
    {
        if (m_api && m_captureRequested)
        {
            m_api->StartFrameCapture(device, window);
            m_capturing = true;
        }
    }

    // Call after the GPU work you want to capture.
    void EndFrameCapture(void* device = nullptr, void* window = nullptr)
    {
        if (m_api && m_capturing)
        {
            m_api->EndFrameCapture(device, window);
            m_capturing = false;
            m_captureRequested = false;
        }
    }

    // Request that the next render call be captured.
    void RequestCapture() { m_captureRequested = true; }

    bool IsCapturing() const { return m_capturing; }

private:
    RENDERDOC_API_1_6_0* m_api{nullptr};
    bool m_captureRequested{false};
    bool m_capturing{false};
};

} // namespace IntegrationTest

#else // !INTEGRATION_TEST_RENDERDOC

namespace IntegrationTest
{

// Stub when RenderDoc support is disabled.
class RenderDocCapture
{
public:
    bool IsAvailable() const { return false; }
    void BeginFrameCapture(void* = nullptr, void* = nullptr) {}
    void EndFrameCapture(void* = nullptr, void* = nullptr) {}
    void RequestCapture() {}
    bool IsCapturing() const { return false; }
};

} // namespace IntegrationTest

#endif // INTEGRATION_TEST_RENDERDOC
