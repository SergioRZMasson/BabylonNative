#include <USDImporter.h>
#include <Babylon/JsRuntime.h>
#include <napi/napi_pointer.h>
#include <arcana/tracing/trace_region.h>
#include <optional>

#define TBB_USE_ASSERT 0
#define TBB_USE_THREADING_TOOLS 0

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primrange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/base/tf/diagnostic.h>

using namespace pxr;

namespace Babylon::Plugins
{
    USDImporter::USDImporter(const Napi::CallbackInfo& info)
        : USDImporter(info, JsRuntime::GetFromJavaScript(info.Env()))
    {
    }

    USDImporter::USDImporter(const Napi::CallbackInfo& info, JsRuntime& runtime)
        : Napi::ObjectWrap<USDImporter>{info}
        , m_cancellationSource{std::make_shared<arcana::cancellation_source>()}
        , m_runtime{runtime}
        , m_runtimeScheduler{runtime}
    {
    }

    USDImporter::~USDImporter()
    {
        Dispose();
    }

    void USDImporter::Dispose()
    {
        m_cancellationSource->cancel();
    }

    void USDImporter::Dispose(const Napi::CallbackInfo& /*info*/)
    {
        Dispose();
    }

    void USDImporter::Initialize(Napi::Env env)
    {
        // Initialize the JavaScript side.
        Napi::HandleScope scope{env};

        // clang-format off
        Napi::Function func = DefineClass(
            env,
            JS_CLASS_NAME,
            {
                // This must match the version in nativeEngine.ts
                InstanceMethod("dispose", &USDImporter::Dispose),
                InstanceMethod("loadUSDFile", &USDImporter::LoadUSDFile),
            });
        // clang-format on

        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_CONSTRUCTOR_NAME, func);
    }

    Napi::Value USDImporter::LoadUSDFile(const Napi::CallbackInfo& info)
    {
        const std::string path{info[0].As<Napi::String>().Utf8Value()};
        
        pxr::UsdStageRefPtr fileStage = pxr::UsdStage::Open(path);

        std::string nodes;

        for (pxr::UsdPrim x : fileStage->Traverse())
        {
            TF_DIAGNOSTIC_WARNING(x.GetName().GetText());
        }

        return Napi::Value::From(info.Env(), 0);;
    }
}
