#include "NativeAudio.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>
#include <thread>
#include <unordered_set>
#include <vector>

namespace LitePlayground
{
    namespace
    {
        constexpr uint32_t SampleRate = 48000;
        constexpr uint32_t BlockFrames = 512;

        bool ValidParameter(bl_AudioParameter parameter)
        {
            return parameter >= BL_AUDIO_GAIN && parameter <= BL_AUDIO_PLAYBACK_RATE;
        }

        struct Automation
        {
            enum class Shape
            {
                Set,
                Linear,
                Exponential,
                Curve
            };
            struct Event
            {
                Shape shape;
                double time;
                double value;
                double duration{};
                std::vector<float> curve;
            };
            double initial{};
            std::vector<Event> events;

            double At(double time) const
            {
                double value = initial;
                double previousTime = 0;
                for (const auto& event : events)
                {
                    if (event.shape == Shape::Linear || event.shape == Shape::Exponential)
                    {
                        if (time < event.time)
                        {
                            const double amount = std::clamp((time - previousTime) / (event.time - previousTime), 0.0, 1.0);
                            return event.shape == Shape::Exponential && value > 0 && event.value > 0 ? value * std::pow(event.value / value, amount) : value + (event.value - value) * amount;
                        }
                    }
                    else if (time < event.time)
                    {
                        break;
                    }
                    if (event.shape == Shape::Curve)
                    {
                        const double position = std::clamp((time - event.time) / event.duration, 0.0, 1.0) * (event.curve.size() - 1);
                        const auto first = static_cast<size_t>(position);
                        const auto last = std::min(first + 1, event.curve.size() - 1);
                        value = event.curve[first] + (event.curve[last] - event.curve[first]) * (position - first);
                        if (time < event.time + event.duration)
                        {
                            return value;
                        }
                        previousTime = event.time + event.duration;
                    }
                    else
                    {
                        value = event.value;
                        previousTime = event.time;
                    }
                }
                return value;
            }

            void Insert(Event event)
            {
                events.push_back(std::move(event));
                std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
            }
        };

        struct Context;
        struct Buffer
        {
            Context* context{};
            double sampleRate{};
            std::vector<std::vector<float>> channels;
            bool released{};
        };

        struct Node
        {
            enum class Kind
            {
                Destination,
                Gain,
                Oscillator,
                Lowpass,
                BufferSource
            };
            struct Edge
            {
                Node* target;
                int parameter{-1};
            };
            Context* context{};
            Kind kind{};
            std::array<Automation, 3> parameters;
            std::vector<Edge> outputs;
            Buffer* buffer{};
            std::vector<std::vector<float>> playback;
            bool loop{};
            bool triangle{};
            bool started{};
            bool released{};
            double start{};
            double stop{INFINITY};
            double phase{};
            double position{};
            double lowpass1{};
            double lowpass2{};
            uint64_t lastSample{UINT64_MAX};
            double sample{};
        };

        struct Context
        {
            std::mutex mutex;
            std::map<Node*, std::unique_ptr<Node>> nodes;
            std::map<Buffer*, std::unique_ptr<Buffer>> buffers;
            Node* destination{};
            Microsoft::WRL::ComPtr<IXAudio2> device;
            IXAudio2MasteringVoice* mastering{};
            IXAudio2SourceVoice* voice{};
            std::thread worker;
            std::atomic<bool> quit{};
            std::atomic<bool> running{};
            std::atomic<bool> output{};
            std::atomic<bool> workerFailed{};
            std::atomic<uint64_t> submitted{};
            std::atomic<uint64_t> nonzero{};
            std::array<std::array<float, BlockFrames * 2>, 3> blocks{};
            uint64_t cursor{};
            uint32_t block{};
            bool closed{};

            ~Context() { Close(); }

            void Close()
            {
                quit = true;
                if (worker.joinable())
                {
                    worker.join();
                }
                if (voice)
                {
                    voice->DestroyVoice();
                    voice = nullptr;
                }
                if (mastering)
                {
                    mastering->DestroyVoice();
                    mastering = nullptr;
                }
                device.Reset();
                closed = true;
                output = false;
            }

            double Time() const
            {
                if (!voice)
                {
                    return static_cast<double>(cursor) / SampleRate;
                }
                XAUDIO2_VOICE_STATE state{};
                voice->GetState(&state);
                return static_cast<double>(state.SamplesPlayed) / SampleRate;
            }

            Node* Create(Node::Kind kind)
            {
                auto node = std::make_unique<Node>();
                node->context = this;
                node->kind = kind;
                node->parameters[BL_AUDIO_GAIN].initial = 1;
                node->parameters[BL_AUDIO_FREQUENCY].initial = 440;
                node->parameters[BL_AUDIO_PLAYBACK_RATE].initial = 1;
                auto* result = node.get();
                nodes.emplace(result, std::move(node));
                return result;
            }

            double Evaluate(Node* node, uint64_t index, uint32_t depth = 0)
            {
                if (depth > nodes.size())
                {
                    return 0;
                }
                if (node->lastSample == index)
                {
                    return node->sample;
                }
                node->lastSample = index;
                node->sample = 0;
                const double time = static_cast<double>(index) / SampleRate;
                const auto parameter = [&](bl_AudioParameter kind) {
                    double value = node->parameters[kind].At(time);
                    for (const auto& [pointer, source] : nodes)
                    {
                        for (const auto& edge : source->outputs)
                        {
                            if (edge.target == node && edge.parameter == kind)
                            {
                                value += Evaluate(pointer, index, depth + 1);
                            }
                        }
                    }
                    return value;
                };
                double input = 0;
                if (node->kind != Node::Kind::Oscillator && node->kind != Node::Kind::BufferSource)
                {
                    for (const auto& [pointer, source] : nodes)
                    {
                        for (const auto& edge : source->outputs)
                        {
                            if (edge.target == node && edge.parameter < 0)
                            {
                                input += Evaluate(pointer, index, depth + 1);
                            }
                        }
                    }
                }
                if (node->kind == Node::Kind::Gain)
                {
                    node->sample = input * parameter(BL_AUDIO_GAIN);
                }
                else if (node->kind == Node::Kind::Lowpass)
                {
                    const double frequency = std::clamp(parameter(BL_AUDIO_FREQUENCY), 1.0, SampleRate * 0.499);
                    const double omega = 2 * std::numbers::pi * frequency / SampleRate;
                    const double cosine = std::cos(omega);
                    const double alpha = std::sin(omega) / 2;
                    const double a0 = 1 + alpha;
                    const double b0 = (1 - cosine) / 2 / a0;
                    const double b1 = (1 - cosine) / a0;
                    const double a1 = -2 * cosine / a0;
                    const double a2 = (1 - alpha) / a0;
                    node->sample = b0 * input + node->lowpass1;
                    node->lowpass1 = b1 * input - a1 * node->sample + node->lowpass2;
                    node->lowpass2 = b0 * input - a2 * node->sample;
                }
                else if (node->kind == Node::Kind::Oscillator && node->started && time >= node->start && time < node->stop)
                {
                    node->sample = node->triangle ? 2 / std::numbers::pi * std::asin(std::sin(node->phase)) : std::sin(node->phase);
                    node->phase = std::fmod(node->phase + 2 * std::numbers::pi * parameter(BL_AUDIO_FREQUENCY) / SampleRate, 2 * std::numbers::pi);
                }
                else if (node->kind == Node::Kind::BufferSource && node->started && time >= node->start && time < node->stop && !node->playback.empty())
                {
                    const size_t frames = node->playback[0].size();
                    if (node->loop)
                    {
                        node->position = std::fmod(node->position, static_cast<double>(frames));
                    }
                    if (node->position < frames)
                    {
                        const auto first = static_cast<size_t>(node->position);
                        const auto next = node->loop ? (first + 1) % frames : std::min(first + 1, frames - 1);
                        for (const auto& channel : node->playback)
                        {
                            node->sample += channel[first] + (channel[next] - channel[first]) * (node->position - first);
                        }
                        node->sample /= node->playback.size();
                        node->position += parameter(BL_AUDIO_PLAYBACK_RATE) * node->buffer->sampleRate / SampleRate;
                    }
                }
                else if (node->kind == Node::Kind::Destination)
                {
                    node->sample = input;
                }
                return node->sample;
            }

            void Prune()
            {
                std::unordered_set<Node*> retained;
                const double time = static_cast<double>(cursor) / SampleRate;
                for (const auto& [pointer, node] : nodes)
                {
                    const bool playing = node->started && time < node->stop &&
                                         (node->kind == Node::Kind::Oscillator || (node->buffer &&
                                                                                      (node->loop || node->position < node->buffer->channels[0].size())));
                    if (!node->released || playing || pointer == destination)
                    {
                        retained.insert(pointer);
                    }
                }
                bool changed = true;
                while (changed)
                {
                    changed = false;
                    for (const auto& [pointer, node] : nodes)
                    {
                        if (retained.contains(pointer))
                        {
                            for (const auto& edge : node->outputs)
                            {
                                changed |= retained.insert(edge.target).second;
                            }
                        }
                    }
                }
                std::erase_if(nodes, [&](const auto& item) { return !retained.contains(item.first); });
                std::erase_if(buffers, [&](const auto& item) {
                    return item.second->released && std::none_of(nodes.begin(), nodes.end(),
                                                        [&](const auto& node) { return node.second->buffer == item.first; });
                });
            }

            void Render()
            {
                try
                {
                    RenderBlocks();
                }
                catch (...)
                {
                    workerFailed = true;
                    output = false;
                    running = false;
                    voice->Stop();
                }
            }

            void RenderBlocks()
            {
                while (!quit)
                {
                    XAUDIO2_VOICE_STATE state{};
                    voice->GetState(&state);
                    if (!running || state.BuffersQueued >= 2)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        continue;
                    }
                    auto& data = blocks[block];
                    {
                        std::lock_guard lock(mutex);
                        Prune();
                        for (uint32_t frame = 0; frame < BlockFrames; ++frame)
                        {
                            const float sample = static_cast<float>(std::clamp(Evaluate(destination, cursor++), -1.0, 1.0));
                            data[frame * 2] = sample;
                            data[frame * 2 + 1] = sample;
                            if (std::abs(sample) > 0.00001f)
                            {
                                ++nonzero;
                            }
                        }
                    }
                    XAUDIO2_BUFFER buffer{};
                    buffer.AudioBytes = static_cast<UINT32>(sizeof(data));
                    buffer.pAudioData = reinterpret_cast<const BYTE*>(data.data());
                    if (FAILED(voice->SubmitSourceBuffer(&buffer)))
                    {
                        output = false;
                        running = false;
                    }
                    else
                    {
                        submitted += BlockFrames;
                        block = (block + 1) % static_cast<uint32_t>(blocks.size());
                    }
                }
            }
        };

        template<typename Callback>
        bl_Status WithNode(bl_HostAudioNode token, Callback callback)
        {
            auto* node = reinterpret_cast<Node*>(token);
            if (!node || !node->context || node->context->closed)
            {
                return BL_INVALID_HANDLE;
            }
            try
            {
                std::lock_guard lock(node->context->mutex);
                return callback(*node);
            }
            catch (...)
            {
                return BL_OUT_OF_MEMORY;
            }
        }

        bl_Status CreateNode(bl_HostAudioContext token, bl_HostAudioNode* result, Node::Kind kind)
        {
            auto* context = reinterpret_cast<Context*>(token);
            if (!context || context->closed || !result)
                return BL_INVALID_ARGUMENT;
            try
            {
                std::lock_guard lock(context->mutex);
                *result = reinterpret_cast<bl_HostAudioNode>(context->Create(kind));
                return BL_OK;
            }
            catch (...)
            {
                return BL_OUT_OF_MEMORY;
            }
        }
    }

    struct NativeAudio::Impl
    {
        std::vector<std::unique_ptr<Context>> contexts;

        bl_Status Create(bl_HostAudioContext* result)
        {
            if (!result)
            {
                return BL_INVALID_ARGUMENT;
            }
            auto context = std::make_unique<Context>();
            HRESULT status = XAudio2Create(&context->device);
            if (SUCCEEDED(status))
                status = context->device->CreateMasteringVoice(&context->mastering);
            WAVEFORMATEX format{};
            format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            format.nChannels = 2;
            format.nSamplesPerSec = SampleRate;
            format.wBitsPerSample = 32;
            format.nBlockAlign = 8;
            format.nAvgBytesPerSec = SampleRate * 8;
            if (SUCCEEDED(status))
                status = context->device->CreateSourceVoice(&context->voice, &format);
            if (FAILED(status))
            {
                return BL_UNSUPPORTED;
            }
            context->destination = context->Create(Node::Kind::Destination);
            context->output = true;
            auto* pointer = context.get();
            contexts.push_back(std::move(context));
            pointer->worker = std::thread([pointer] { pointer->Render(); });
            *result = reinterpret_cast<bl_HostAudioContext>(pointer);
            return BL_OK;
        }
    };

    NativeAudio::NativeAudio()
        : m_impl(std::make_unique<Impl>())
    {
    }
    NativeAudio::~NativeAudio() = default;

    bl_AudioService NativeAudio::Service()
    {
        bl_AudioService service{};
        service.userData = m_impl.get();
        service.createContext = [](void* user, bl_HostAudioContext* result) {
            try
            {
                return static_cast<Impl*>(user)->Create(result);
            }
            catch (...)
            {
                return BL_OUT_OF_MEMORY;
            }
        };
        service.closeContext = [](void*, bl_HostAudioContext context) {
            auto* value = reinterpret_cast<Context*>(context);
            if (!value)
                return BL_INVALID_HANDLE;
            value->Close();
            return BL_OK;
        };
        service.resumeContext = [](void*, bl_HostAudioContext context) {
            auto* value = reinterpret_cast<Context*>(context);
            if (!value || value->closed)
                return BL_INVALID_HANDLE;
            if (value->workerFailed)
                return BL_HOST_ERROR;
            if (FAILED(value->voice->Start()))
                return BL_HOST_ERROR;
            value->running = true;
            return BL_OK;
        };
        service.getContextInfo = [](void*, bl_HostAudioContext context, bl_HostAudioInfo* info) {
            auto* value = reinterpret_cast<Context*>(context);
            if (!value || !info)
                return BL_INVALID_ARGUMENT;
            *info = {value->closed ? BL_AUDIO_CLOSED : value->running ? BL_AUDIO_RUNNING
                                                                      : BL_AUDIO_STATE_SUSPENDED,
                value->Time(), SampleRate, false, value->output.load()};
            return BL_OK;
        };
        service.getDestination = [](void*, bl_HostAudioContext context, bl_HostAudioNode* node) {
            auto* value = reinterpret_cast<Context*>(context);
            if (!value || value->closed || !node)
                return BL_INVALID_ARGUMENT;
            *node = reinterpret_cast<bl_HostAudioNode>(value->destination);
            return BL_OK;
        };
        service.createGain = +[](void*, bl_HostAudioContext c, bl_HostAudioNode* n) { return CreateNode(c, n, Node::Kind::Gain); };
        service.createOscillator = +[](void*, bl_HostAudioContext c, bl_HostAudioNode* n) { return CreateNode(c, n, Node::Kind::Oscillator); };
        service.createBiquadLowpass = +[](void*, bl_HostAudioContext c, bl_HostAudioNode* n) { return CreateNode(c, n, Node::Kind::Lowpass); };
        service.createBufferSource = +[](void*, bl_HostAudioContext c, bl_HostAudioNode* n) { return CreateNode(c, n, Node::Kind::BufferSource); };
        service.createBuffer = [](void*, bl_HostAudioContext token, uint32_t channels, size_t frames, double rate, bl_HostAudioBuffer* result) {
            auto* context = reinterpret_cast<Context*>(token);
            if (!context || context->closed || !channels || !frames || !std::isfinite(rate) || rate <= 0 || !result)
                return BL_INVALID_ARGUMENT;
            if (channels != 1)
                return BL_UNSUPPORTED;
            try
            {
                auto buffer = std::make_unique<Buffer>();
                buffer->context = context;
                buffer->sampleRate = rate;
                buffer->channels.resize(channels, std::vector<float>(frames));
                auto* pointer = buffer.get();
                std::lock_guard lock(context->mutex);
                context->buffers.emplace(pointer, std::move(buffer));
                *result = reinterpret_cast<bl_HostAudioBuffer>(pointer);
                return BL_OK;
            }
            catch (...)
            {
                return BL_OUT_OF_MEMORY;
            }
        };
        service.getChannelData = [](void*, bl_HostAudioBuffer token, uint32_t channel, float** samples, size_t* frames) {
            auto* buffer = reinterpret_cast<Buffer*>(token);
            if (!buffer || channel >= buffer->channels.size() || !samples || !frames)
                return BL_INVALID_ARGUMENT;
            *samples = buffer->channels[channel].data();
            *frames = buffer->channels[channel].size();
            return BL_OK;
        };
        service.setBuffer = [](void*, bl_HostAudioNode token, bl_HostAudioBuffer value) {
            return WithNode(token, [&](Node& node) {
                auto* buffer = reinterpret_cast<Buffer*>(value);
                if (!buffer || buffer->context != node.context || node.started)
                    return BL_INVALID_ARGUMENT;
                node.buffer = buffer;
                return BL_OK;
            });
        };
        service.setLoop = [](void*, bl_HostAudioNode token, bool loop) {
            return WithNode(token, [&](Node& n) {
                if (n.kind != Node::Kind::BufferSource)
                    return BL_INVALID_ARGUMENT;
                n.loop = loop;
                return BL_OK;
            });
        };
        service.setOscillatorType = [](void*, bl_HostAudioNode token, bl_OscillatorType type) {
            return WithNode(token, [&](Node& n) {
                if (n.kind != Node::Kind::Oscillator)
                    return BL_INVALID_ARGUMENT;
                if (type != BL_OSCILLATOR_SINE && type != BL_OSCILLATOR_TRIANGLE)
                    return BL_UNSUPPORTED;
                n.triangle = type == BL_OSCILLATOR_TRIANGLE;
                return BL_OK;
            });
        };
        service.connect = [](void*, bl_HostAudioNode source, bl_HostAudioNode target) {
            return WithNode(source, [&](Node& node) {
                auto* destination = reinterpret_cast<Node*>(target);
                if (!destination || destination->context != node.context)
                    return BL_INVALID_ARGUMENT;
                node.outputs.push_back({destination});
                return BL_OK;
            });
        };
        service.connectParameter = [](void*, bl_HostAudioNode source, bl_HostAudioNode target, bl_AudioParameter parameter) {
            return WithNode(source, [&](Node& node) {
                auto* destination = reinterpret_cast<Node*>(target);
                if (!destination || destination->context != node.context || !ValidParameter(parameter))
                    return BL_INVALID_ARGUMENT;
                node.outputs.push_back({destination, parameter});
                return BL_OK;
            });
        };
        service.disconnect = [](void*, bl_HostAudioNode token) { return WithNode(token, [](Node& n) { n.outputs.clear(); return BL_OK; }); };
        service.setParameter = [](void*, bl_HostAudioNode token, bl_AudioParameter parameter, double value, double time) {
            return WithNode(token, [&](Node& n) {
                if (!ValidParameter(parameter) || !std::isfinite(value) || !std::isfinite(time) || time < 0)
                    return BL_INVALID_ARGUMENT;
                if (parameter == BL_AUDIO_PLAYBACK_RATE && value < 0)
                    return BL_UNSUPPORTED;
                n.parameters[parameter].Insert({Automation::Shape::Set, time, value});
                return BL_OK;
            });
        };
        service.getParameter = [](void*, bl_HostAudioNode token, bl_AudioParameter parameter, double* value) {
            return WithNode(token, [&](Node& n) {
                if (!ValidParameter(parameter) || !value)
                    return BL_INVALID_ARGUMENT;
                *value = n.parameters[parameter].At(n.context->Time());
                return BL_OK;
            });
        };
        service.cancelScheduledParameter = [](void*, bl_HostAudioNode token, bl_AudioParameter parameter, double time) {
            return WithNode(token, [&](Node& n) {
                if (!ValidParameter(parameter) || !std::isfinite(time) || time < 0)
                    return BL_INVALID_ARGUMENT;
                std::erase_if(n.parameters[parameter].events, [&](const auto& event) { return event.time >= time; });
                return BL_OK;
            });
        };
        service.setParameterCurve = [](void*, bl_HostAudioNode token, bl_AudioParameter parameter, bl_F32Span values, double time, double duration) {
            return WithNode(token, [&](Node& n) {
                if (!ValidParameter(parameter) || !values.data || values.count < 2 || !std::isfinite(time) || time < 0 ||
                    !std::isfinite(duration) || duration <= 0)
                    return BL_INVALID_ARGUMENT;
                for (size_t index = 0; index < values.count; ++index)
                {
                    if (!std::isfinite(values.data[index]))
                        return BL_INVALID_ARGUMENT;
                    if (parameter == BL_AUDIO_PLAYBACK_RATE && values.data[index] < 0)
                        return BL_UNSUPPORTED;
                }
                Automation::Event event{Automation::Shape::Curve, time, 0, duration, {values.data, values.data + values.count}};
                n.parameters[parameter].Insert(std::move(event));
                return BL_OK;
            });
        };
        service.linearRampParameter = [](void*, bl_HostAudioNode token, bl_AudioParameter parameter, double value, double time) {
            return WithNode(token, [&](Node& n) {
                if (!ValidParameter(parameter) || !std::isfinite(value) || !std::isfinite(time) || time < 0)
                    return BL_INVALID_ARGUMENT;
                if (parameter == BL_AUDIO_PLAYBACK_RATE && value < 0)
                    return BL_UNSUPPORTED;
                n.parameters[parameter].Insert({Automation::Shape::Linear, time, value});
                return BL_OK;
            });
        };
        service.exponentialRampParameter = [](void*, bl_HostAudioNode token, bl_AudioParameter parameter, double value, double time) {
            return WithNode(token, [&](Node& n) {
                if (!ValidParameter(parameter) || !std::isfinite(value) || value <= 0 || !std::isfinite(time) || time < 0)
                    return BL_INVALID_ARGUMENT;
                n.parameters[parameter].Insert({Automation::Shape::Exponential, time, value});
                return BL_OK;
            });
        };
        service.start = [](void*, bl_HostAudioNode token, double time) {
            return WithNode(token, [&](Node& n) {
                if ((n.kind != Node::Kind::BufferSource && n.kind != Node::Kind::Oscillator) ||
                    n.started || !std::isfinite(time) || time < 0)
                    return BL_INVALID_ARGUMENT;
                if (n.kind == Node::Kind::BufferSource)
                {
                    if (!n.buffer)
                        return BL_INVALID_ARGUMENT;
                    n.playback = n.buffer->channels;
                }
                n.start = time;
                n.started = true;
                return BL_OK;
            });
        };
        service.stop = [](void*, bl_HostAudioNode token, double time) {
            return WithNode(token, [&](Node& n) {
                if (!n.started || !std::isfinite(time) || time < 0)
                    return BL_INVALID_ARGUMENT;
                n.stop = time;
                return BL_OK;
            });
        };
        service.releaseNode = [](void*, bl_HostAudioNode token) { WithNode(token, [](Node& n) { n.released = true; return BL_OK; }); };
        service.releaseBuffer = [](void*, bl_HostAudioBuffer token) {
            auto* buffer = reinterpret_cast<Buffer*>(token);
            if (buffer)
            {
                std::lock_guard lock(buffer->context->mutex);
                buffer->released = true;
            }
        };
        return service;
    }

    uint64_t NativeAudio::SubmittedFrames() const
    {
        uint64_t result{};
        for (const auto& context : m_impl->contexts)
            result += context->submitted;
        return result;
    }
    uint64_t NativeAudio::NonzeroSamples() const
    {
        uint64_t result{};
        for (const auto& context : m_impl->contexts)
            result += context->nonzero;
        return result;
    }
    bool NativeAudio::OutputAvailable() const
    {
        return std::any_of(m_impl->contexts.begin(), m_impl->contexts.end(), [](const auto& context) { return context->output.load(); });
    }
}
