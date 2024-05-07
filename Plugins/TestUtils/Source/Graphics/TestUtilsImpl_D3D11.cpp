#include <windows.h>
#include "../TestUtilsImplData.h"
#include "renderdoc_app.h"

#ifdef BABYLON_TEST_UTILS_RENDER_DOC_DEBUG

RENDERDOC_API_1_6_0* rdoc_api = NULL;

namespace Babylon::Plugins::Internal
{
    void InitializeGraphics()
    {
        if (HMODULE mod = GetModuleHandleW(L"renderdoc.dll"))
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
            int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&rdoc_api);

            if (ret == 0)
            {
                OutputDebugString("ERROR: unable to load Render Doc with API version 1.6.0 \n");
            }
            else
            {
                rdoc_api->SetCaptureTitle("Babylon Native Playground capture");
                rdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks, 1);
            }
        }
        else
        {
            OutputDebugString("ERROR: renderdoc.dll not found \n");
        }
    }

    void TestUtils::StartGraphicsDebug(const Napi::CallbackInfo&)
    {
        if (rdoc_api)
        {
            rdoc_api->StartFrameCapture(NULL, m_implData->m_window);
        }
    }

    void TestUtils::EndGraphicsDebug(const Napi::CallbackInfo&)
    {
        if (rdoc_api)
        {
            rdoc_api->EndFrameCapture(NULL, m_implData->m_window);
        }
    }
}
#else

namespace Babylon::Plugins::Internal
{
    void InitializeGraphics()
    {
    }

    void TestUtils::StartGraphicsDebug(const Napi::CallbackInfo&)
    {
    }

    void TestUtils::EndGraphicsDebug(const Napi::CallbackInfo&)
    {
    }
}

#endif // BABYLON_TEST_UTILS_RENDER_DOC_DEBUG
