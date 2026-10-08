#pragma once

#include <babylon_lite.h>
#include <napi/napi.h>

namespace Babylon::Plugins::LiteJSBinding
{
    struct HostOptions
    {
        bl_Runtime* runtime{};
        bl_NativeEngineOptions nativeEngine{};
    };

    struct FrameTimings
    {
        double nativeFrameIncludingCallbacksMs{};
        double userCallbacksMs{};
    };

    // All calls, runtime disposal and binding disposal run on the JS/runtime
    // creating thread. Host owns the runtime, window and injected services.
    void Initialize(Napi::Env env, const HostOptions& options);
    void Frame(Napi::Env env, double deltaMs);
    FrameTimings GetFrameTimings(Napi::Env env);
    // Invoke after successful C99 runtime cascading disposal. Settles cancellation
    // promises and can restore a captured user exception after invalidating tokens.
    void Dispose(Napi::Env env);
}
