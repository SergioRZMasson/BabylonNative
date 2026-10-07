#include "C99Client.h"
#include <fstream>

namespace bbl::pal
{
    AudioNodeData::~AudioNodeData()
    {
        if (LiteMinecraft::Runtime())
        {
            const auto& service = LiteMinecraft::AudioService();
            service.releaseNode(service.userData, native);
        }
    }

    AudioBufferData::~AudioBufferData()
    {
        if (LiteMinecraft::Runtime())
        {
            const auto& service = LiteMinecraft::AudioService();
            service.releaseBuffer(service.userData, native);
        }
    }

    namespace
    {
        const bl_AudioService& Service()
        {
            return LiteMinecraft::AudioService();
        }

        bl_HostAudioInfo Info(AudioContextHandle context)
        {
            bl_HostAudioInfo info{};
            Check(Service().getContextInfo(Service().userData, context, &info));
            return info;
        }
    }

    std::vector<uint8_t> read_binary_file(const std::string& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Missing original application pixels: " + path);
        }
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    double audio_sample_rate(AudioContextHandle context)
    {
        return Info(context).sampleRate;
    }

    double audio_current_time(AudioContextHandle context)
    {
        return Info(context).currentTime;
    }

    AudioNodeHandle audio_create_gain(AudioContextHandle context)
    {
        auto node = std::make_shared<AudioNodeData>();
        Check(Service().createGain(Service().userData, context, &node->native));
        return node;
    }

    AudioNodeHandle audio_create_oscillator(AudioContextHandle context)
    {
        auto node = std::make_shared<AudioNodeData>();
        Check(Service().createOscillator(Service().userData, context, &node->native));
        return node;
    }

    AudioNodeHandle audio_create_biquad_filter(AudioContextHandle context)
    {
        auto node = std::make_shared<AudioNodeData>();
        Check(Service().createBiquadLowpass(Service().userData, context, &node->native));
        return node;
    }

    AudioNodeHandle audio_create_buffer_source(AudioContextHandle context)
    {
        auto node = std::make_shared<AudioNodeData>();
        Check(Service().createBufferSource(Service().userData, context, &node->native));
        return node;
    }

    AudioBufferHandle audio_create_buffer(AudioContextHandle context, uint32_t channels,
                                          uint32_t frames, double sampleRate)
    {
        if (channels != 1)
        {
            throw std::runtime_error("Native application audio transport requires mono.");
        }
        auto buffer = std::make_shared<AudioBufferData>();
        Check(Service().createBuffer(Service().userData, context, channels, frames, sampleRate,
                                     &buffer->native));
        buffer->samples = js::F32Array(frames);
        return buffer;
    }

    js::F32Array audio_buffer_channel(AudioBufferHandle buffer, uint32_t channel)
    {
        if (channel != 0)
        {
            throw std::runtime_error("Invalid native audio channel.");
        }
        return buffer->samples;
    }

    AudioParamHandle audio_node_param(AudioNodeHandle node, AudioParamName parameter)
    {
        return {node, static_cast<bl_AudioParameter>(parameter)};
    }

    void audio_param_set_value(AudioParamHandle parameter, double value)
    {
        Check(Service().setParameter(Service().userData, parameter.node->native,
                                     parameter.parameter, value, 0));
    }

    void audio_param_set_value_at_time(AudioParamHandle parameter, double value, double time)
    {
        Check(Service().setParameter(Service().userData, parameter.node->native,
                                     parameter.parameter, value, time));
    }

    void audio_param_exponential_ramp(AudioParamHandle parameter, double value, double time)
    {
        Check(Service().exponentialRampParameter(Service().userData, parameter.node->native,
                                                 parameter.parameter, value, time));
    }

    void audio_connect(AudioNodeHandle source, AudioNodeHandle target)
    {
        Check(Service().connect(Service().userData, source->native, target->native));
    }

    void audio_connect_param(AudioNodeHandle source, AudioParamHandle target)
    {
        Check(Service().connectParameter(Service().userData, source->native, target.node->native,
                                         target.parameter));
    }

    void audio_set_buffer(AudioNodeHandle node, AudioBufferHandle buffer)
    {
        float* samples{};
        size_t frames{};
        Check(Service().getChannelData(Service().userData, buffer->native, 0, &samples, &frames));
        if (frames != buffer->samples.size())
        {
            throw std::runtime_error("Audio transport channel length mismatch.");
        }
        std::copy_n(buffer->samples.data(), frames, samples);
        Check(Service().setBuffer(Service().userData, node->native, buffer->native));
    }

    void audio_set_loop(AudioNodeHandle node, bool loop)
    {
        Check(Service().setLoop(Service().userData, node->native, loop));
    }

    void audio_set_filter_kind(AudioNodeHandle, BiquadFilterKind)
    {
        // createBiquadLowpass already creates the only original reached filter type.
    }

    void audio_set_oscillator_wave(AudioNodeHandle node, OscillatorWave wave)
    {
        Check(Service().setOscillatorType(Service().userData, node->native,
                                          static_cast<bl_OscillatorType>(wave)));
    }

    void audio_node_start(AudioNodeHandle node, double time)
    {
        Check(Service().start(Service().userData, node->native, time));
    }

    void audio_node_stop(AudioNodeHandle node, double time)
    {
        Check(Service().stop(Service().userData, node->native, time));
    }
}
