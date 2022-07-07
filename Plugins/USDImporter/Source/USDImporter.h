#pragma once

#include <Babylon/JsRuntime.h>
#include <Babylon/JsRuntimeScheduler.h>

#include <napi/napi.h>

#include <arcana/threading/cancellation.h>
#include <unordered_map>


namespace Babylon::Plugins
{
    class USDImporter final : public Napi::ObjectWrap<USDImporter>
    {
        static constexpr auto JS_CLASS_NAME = "_USDImporter";
        static constexpr auto JS_CONSTRUCTOR_NAME = "USDImporter";

    public:
        USDImporter(const Napi::CallbackInfo& info);
        USDImporter(const Napi::CallbackInfo& info, JsRuntime& runtime);
        ~USDImporter();

        static void Initialize(Napi::Env env);

    private:
        void Dispose();
        void Dispose(const Napi::CallbackInfo& info);
        Napi::Value LoadUSDFile(const Napi::CallbackInfo& info);

        JsRuntime& m_runtime;
        JsRuntimeScheduler m_runtimeScheduler;
        std::shared_ptr<arcana::cancellation_source> m_cancellationSource{};
    };
}
