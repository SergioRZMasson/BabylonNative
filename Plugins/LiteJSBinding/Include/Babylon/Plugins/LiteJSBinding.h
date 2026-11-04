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
    // Additive host opt-in; preserves HostOptions' existing layout. Reserve the
    // palette slot as documented by bl_setNativeClearPaletteIndex.
    void Initialize(Napi::Env env, const HostOptions& options, uint8_t clearPaletteIndex);
    void Frame(Napi::Env env, double deltaMs);
    FrameTimings GetFrameTimings(Napi::Env env);
    // Invoke after successful C99 runtime cascading disposal. Settles cancellation
    // promises and can restore a captured user exception after invalidating tokens.
    void Dispose(Napi::Env env);
}
