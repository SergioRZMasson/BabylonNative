#pragma once

#include <babylon_lite.h>
#include <napi/napi.h>
#include <memory>

namespace Babylon::Plugins::LiteJSBinding
{
    class AudioBinding
    {
    public:
        AudioBinding(bl_HostAudioContext context, const bl_AudioService& service);
        ~AudioBinding();
        Napi::Object Wrap(Napi::Env env);
        bl_HostAudioNode Node(Napi::Value value);
        void Invalidate();

    private:
        struct Impl;
        std::shared_ptr<Impl> m_impl;
    };
}
