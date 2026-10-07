#pragma once

#include <babylon_lite.h>
#include <memory>

namespace LitePlayground
{
    class NativeAudio
    {
    public:
        NativeAudio();
        ~NativeAudio();
        bl_AudioService Service();
        uint64_t SubmittedFrames() const;
        uint64_t NonzeroSamples() const;
        bool OutputAvailable() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
