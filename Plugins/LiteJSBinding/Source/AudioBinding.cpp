#include "AudioBinding.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace Babylon::Plugins::LiteJSBinding
{
    namespace
    {
        void Check(Napi::Env env, bl_Status status)
        {
            if (status != BL_OK)
            {
                auto error = Napi::Error::New(env, "Native audio service status " + std::to_string(status));
                error.Set("status", static_cast<double>(status));
                throw error;
            }
        }

        double Number(const Napi::CallbackInfo& info, size_t index, double fallback = 0)
        {
            return index < info.Length() && !info[index].IsUndefined() ? info[index].As<Napi::Number>().DoubleValue() : fallback;
        }

        void Method(Napi::Object object, const char* name, std::function<Napi::Value(const Napi::CallbackInfo&)> callback)
        {
            object.Set(name, Napi::Function::New(object.Env(), [callback](const Napi::CallbackInfo& info) {
                try
                {
                    return callback(info);
                }
                catch (const Napi::Error& error)
                {
                    error.ThrowAsJavaScriptException();
                }
                catch (const std::exception& error)
                {
                    Napi::Error::New(info.Env(), error.what()).ThrowAsJavaScriptException();
                }
                return info.Env().Undefined();
            }));
        }

        Napi::PropertyDescriptor Accessor(Napi::Object object, const char* name,
            std::function<Napi::Value(const Napi::CallbackInfo&)> getter,
            std::function<void(const Napi::CallbackInfo&, Napi::Value)> setter = {})
        {
            const auto read = [getter](const Napi::CallbackInfo& info) {
                return getter ? getter(info) : info.Env().Undefined();
            };
            if (!setter)
            {
                return Napi::PropertyDescriptor::Accessor(object.Env(), object, name, read);
            }
            return Napi::PropertyDescriptor::Accessor(object.Env(), object, name, read,
                [setter](const Napi::CallbackInfo& info) { setter(info, info[0]); });
        }
    }

    struct AudioBinding::Impl : std::enable_shared_from_this<Impl>
    {
        struct Buffer;
        struct Node
        {
            std::shared_ptr<Impl> owner;
            bl_HostAudioNode handle{};
            std::shared_ptr<Buffer> buffer;
            Napi::ObjectReference bufferObject;
            bool loop{};
            std::string oscillatorType{"sine"};
            bool borrowed{};
            ~Node()
            {
                owner->nodes.erase(this);
                if (!borrowed && owner->Live())
                {
                    owner->service.releaseNode(owner->service.userData, handle);
                }
            }
        };
        struct Buffer
        {
            std::shared_ptr<Impl> owner;
            bl_HostAudioBuffer handle{};
            std::vector<Napi::Reference<Napi::Float32Array>> channels;
            ~Buffer()
            {
                owner->buffers.erase(this);
                if (owner->Live())
                {
                    owner->service.releaseBuffer(owner->service.userData, handle);
                }
            }
            void Flush(Napi::Env env)
            {
                for (uint32_t channel = 0; channel < channels.size(); ++channel)
                {
                    float* data{};
                    size_t count{};
                    Check(env, owner->service.getChannelData(owner->service.userData, handle, channel, &data, &count));
                    const auto source = channels[channel].Value();
                    if (source.ElementLength() != count)
                    {
                        throw Napi::Error::New(env, "Native audio channel size changed.");
                    }
                    std::memcpy(data, source.Data(), count * sizeof(float));
                }
            }
        };

        bl_HostAudioContext context{};
        bl_AudioService service{};
        bool invalidated{};
        Napi::ObjectReference contextObject;
        std::unordered_set<Node*> nodes;
        std::unordered_set<Buffer*> buffers;

        bool Live() const
        {
            bl_HostAudioInfo info{};
            return !invalidated && service.getContextInfo(service.userData, context, &info) == BL_OK && info.state != BL_AUDIO_CLOSED;
        }

        void Require(Napi::Env env) const
        {
            if (!Live())
            {
                throw Napi::Error::New(env, "The native audio context has closed.");
            }
        }

        std::shared_ptr<Node> GetNode(Napi::Value value)
        {
            Require(value.Env());
            if (!value.IsObject())
            {
                throw Napi::TypeError::New(value.Env(), "Expected a native AudioNode.");
            }
            const auto token = value.As<Napi::Object>().Get("_hostAudioNode");
            if (!token.IsExternal())
            {
                throw Napi::TypeError::New(value.Env(), "Expected a native AudioNode.");
            }
            auto* holder = token.As<Napi::External<std::shared_ptr<Node>>>().Data();
            // Only External pointers issued by this binding are dereferenced.
            const auto found = nodeTokens.find(holder);
            if (found == nodeTokens.end())
            {
                throw Napi::TypeError::New(value.Env(), "AudioNode belongs to another audio context.");
            }
            return *holder;
        }

        std::unordered_set<std::shared_ptr<Node>*> nodeTokens;
        std::unordered_set<std::shared_ptr<Buffer>*> bufferTokens;

        Napi::Object Parameter(Napi::Env env, const std::shared_ptr<Node>& node, bl_AudioParameter parameter)
        {
            auto object = Napi::Object::New(env);
            object.DefineProperties({Accessor(object, "value", [node, parameter](const Napi::CallbackInfo& info) {
                    node->owner->Require(info.Env());
                    double value{};
                    Check(info.Env(), node->owner->service.getParameter(node->owner->service.userData, node->handle, parameter, &value));
                    return Napi::Number::New(info.Env(), value); }, [node, parameter](const Napi::CallbackInfo& info, Napi::Value value) {
                    auto& owner = *node->owner;
                    owner.Require(info.Env());
                    bl_HostAudioInfo state{};
                    Check(info.Env(), owner.service.getContextInfo(owner.service.userData, owner.context, &state));
                    Check(info.Env(), owner.service.setParameter(owner.service.userData, node->handle, parameter,
                        value.As<Napi::Number>().DoubleValue(), state.currentTime)); })});
            const auto schedule = [&](const char* name, auto callback) {
                Method(object, name, [node, parameter, callback](const Napi::CallbackInfo& info) {
                    auto& owner = *node->owner;
                    owner.Require(info.Env());
                    Check(info.Env(), callback(owner.service, node->handle, parameter, Number(info, 0), Number(info, 1)));
                    return info.This();
                });
            };
            schedule("setValueAtTime", [](const auto& s, auto n, auto p, double v, double t) { return s.setParameter(s.userData, n, p, v, t); });
            schedule("linearRampToValueAtTime", [](const auto& s, auto n, auto p, double v, double t) { return s.linearRampParameter(s.userData, n, p, v, t); });
            schedule("exponentialRampToValueAtTime", [](const auto& s, auto n, auto p, double v, double t) { return s.exponentialRampParameter(s.userData, n, p, v, t); });
            Method(object, "cancelScheduledValues", [node, parameter](const Napi::CallbackInfo& info) {
                auto& owner = *node->owner;
                owner.Require(info.Env());
                Check(info.Env(), owner.service.cancelScheduledParameter(owner.service.userData, node->handle, parameter, Number(info, 0)));
                return info.This();
            });
            Method(object, "setValueCurveAtTime", [node, parameter](const Napi::CallbackInfo& info) {
                auto& owner = *node->owner;
                owner.Require(info.Env());
                const auto array = info[0].As<Napi::Float32Array>();
                Check(info.Env(), owner.service.setParameterCurve(owner.service.userData, node->handle, parameter,
                                      {array.Data(), array.ElementLength()}, Number(info, 1), Number(info, 2)));
                return info.This();
            });
            object.Set("_parameterNode", Napi::External<Node>::New(env, node.get()));
            object.Set("_parameterKind", static_cast<int>(parameter));
            return object;
        }

        Napi::Object WrapNode(Napi::Env env, bl_HostAudioNode handle, const std::string& kind, bool borrowed = false)
        {
            auto owner = shared_from_this();
            auto node = std::make_shared<Node>();
            node->owner = owner;
            node->handle = handle;
            node->borrowed = borrowed;
            nodes.insert(node.get());
            auto object = Napi::Object::New(env);
            if (!contextObject.Value().IsEmpty())
            {
                object.Set("context", contextObject.Value());
            }
            auto* token = new std::shared_ptr<Node>(node);
            nodeTokens.insert(token);
            object.Set("_hostAudioNode", Napi::External<std::shared_ptr<Node>>::New(env, token,
                                             [owner](Napi::Env, std::shared_ptr<Node>* finalized) {
                                                 owner->nodeTokens.erase(finalized);
                                                 delete finalized;
                                             }));
            if (kind == "gain")
                object.Set("gain", Parameter(env, node, BL_AUDIO_GAIN));
            if (kind == "oscillator" || kind == "filter")
                object.Set("frequency", Parameter(env, node, BL_AUDIO_FREQUENCY));
            if (kind == "bufferSource")
                object.Set("playbackRate", Parameter(env, node, BL_AUDIO_PLAYBACK_RATE));
            Method(object, "connect", [node](const Napi::CallbackInfo& info) {
                auto& owner = *node->owner;
                owner.Require(info.Env());
                auto target = info[0].As<Napi::Object>();
                const auto field = target.Get("_parameterNode");
                if (field.IsExternal())
                {
                    auto* pointer = field.As<Napi::External<Node>>().Data();
                    if (!owner.nodes.contains(pointer))
                    {
                        throw Napi::TypeError::New(info.Env(), "AudioParam belongs to another context.");
                    }
                    Check(info.Env(), owner.service.connectParameter(owner.service.userData, node->handle, pointer->handle,
                                          static_cast<bl_AudioParameter>(target.Get("_parameterKind").As<Napi::Number>().Int32Value())));
                }
                else
                {
                    auto destination = owner.GetNode(target);
                    Check(info.Env(), owner.service.connect(owner.service.userData, node->handle, destination->handle));
                }
                return info[0];
            });
            Method(object, "disconnect", [node](const Napi::CallbackInfo& info) {
                node->owner->Require(info.Env());
                Check(info.Env(), node->owner->service.disconnect(node->owner->service.userData, node->handle));
                return info.Env().Undefined();
            });
            if (kind == "oscillator" || kind == "bufferSource")
            {
                Method(object, "start", [node](const Napi::CallbackInfo& info) {
                    node->owner->Require(info.Env());
                    if (node->buffer)
                        node->buffer->Flush(info.Env());
                    Check(info.Env(), node->owner->service.start(node->owner->service.userData, node->handle, Number(info, 0)));
                    return info.Env().Undefined();
                });
                Method(object, "stop", [node](const Napi::CallbackInfo& info) {
                    node->owner->Require(info.Env());
                    Check(info.Env(), node->owner->service.stop(node->owner->service.userData, node->handle, Number(info, 0)));
                    return info.Env().Undefined();
                });
            }
            if (kind == "oscillator")
            {
                object.DefineProperties({Accessor(object, "type", [node](const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), node->oscillatorType); }, [node](const Napi::CallbackInfo& info, Napi::Value value) {
                        node->owner->Require(info.Env());
                        const auto type = value.As<Napi::String>().Utf8Value();
                        if (type != "sine" && type != "triangle") throw Napi::TypeError::New(info.Env(), "Unsupported native oscillator shape.");
                        Check(info.Env(), node->owner->service.setOscillatorType(node->owner->service.userData, node->handle,
                            type == "triangle" ? BL_OSCILLATOR_TRIANGLE : BL_OSCILLATOR_SINE));
                        node->oscillatorType = type; })});
            }
            if (kind == "filter")
            {
                object.DefineProperties({Accessor(object, "type", [](const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), "lowpass"); }, [](const Napi::CallbackInfo& info, Napi::Value value) {
                        if (value.As<Napi::String>().Utf8Value() != "lowpass") throw Napi::TypeError::New(info.Env(), "Only native lowpass filtering is supported."); })});
            }
            if (kind == "bufferSource")
            {
                object.DefineProperties({Accessor(object, "loop", [node](const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), node->loop); }, [node](const Napi::CallbackInfo& info, Napi::Value value) {
                        node->owner->Require(info.Env());
                        Check(info.Env(), node->owner->service.setLoop(node->owner->service.userData, node->handle, value.ToBoolean().Value()));
                        node->loop = value.ToBoolean().Value(); }), Accessor(object, "buffer", [node](const Napi::CallbackInfo& info) -> Napi::Value { return node->bufferObject.IsEmpty() ? info.Env().Null() : node->bufferObject.Value(); }, [node](const Napi::CallbackInfo& info, Napi::Value value) {
                        auto& owner = *node->owner;
                        owner.Require(info.Env());
                        const auto external = value.As<Napi::Object>().Get("_hostAudioBuffer");
                        if (!external.IsExternal()) throw Napi::TypeError::New(info.Env(), "Expected a native AudioBuffer.");
                        auto* token = external.As<Napi::External<std::shared_ptr<Buffer>>>().Data();
                        if (!owner.bufferTokens.contains(token)) throw Napi::TypeError::New(info.Env(), "AudioBuffer belongs to another context.");
                        Check(info.Env(), owner.service.setBuffer(owner.service.userData, node->handle, (*token)->handle));
                        node->buffer = *token;
                        node->bufferObject = Napi::Persistent(value.As<Napi::Object>()); })});
            }
            return object;
        }
    };

    AudioBinding::AudioBinding(bl_HostAudioContext context, const bl_AudioService& service)
        : m_impl(std::make_shared<Impl>())
    {
        m_impl->context = context;
        m_impl->service = service;
    }
    AudioBinding::~AudioBinding() = default;
    void AudioBinding::Invalidate() { m_impl->invalidated = true; }
    bl_HostAudioNode AudioBinding::Node(Napi::Value value) { return m_impl->GetNode(value)->handle; }

    Napi::Object AudioBinding::Wrap(Napi::Env env)
    {
        auto owner = m_impl;
        auto object = Napi::Object::New(env);
        owner->contextObject = Napi::Persistent(object);
        owner->contextObject.Unref();
        for (const auto* name : {"currentTime", "sampleRate", "state"})
        {
            object.DefineProperties({Accessor(object, name, [owner, key = std::string(name)](const Napi::CallbackInfo& info) -> Napi::Value {
                owner->Require(info.Env());
                bl_HostAudioInfo state{};
                Check(info.Env(), owner->service.getContextInfo(owner->service.userData, owner->context, &state));
                if (key == "state")
                    return Napi::String::New(info.Env(), state.state == BL_AUDIO_RUNNING ? "running" : "suspended");
                return Napi::Number::New(info.Env(), key == "currentTime" ? state.currentTime : state.sampleRate);
            })});
        }
        bl_HostAudioNode destination{};
        Check(env, owner->service.getDestination(owner->service.userData, owner->context, &destination));
        object.Set("destination", owner->WrapNode(env, destination, "destination", true));
        const auto create = [&](const char* name, const char* kind, auto callback) {
            Method(object, name, [owner, kind = std::string(kind), callback](const Napi::CallbackInfo& info) {
                owner->Require(info.Env());
                bl_HostAudioNode node{};
                Check(info.Env(), callback(owner->service.userData, owner->context, &node));
                return owner->WrapNode(info.Env(), node, kind);
            });
        };
        create("createGain", "gain", owner->service.createGain);
        create("createOscillator", "oscillator", owner->service.createOscillator);
        create("createBiquadFilter", "filter", owner->service.createBiquadLowpass);
        create("createBufferSource", "bufferSource", owner->service.createBufferSource);
        Method(object, "createBuffer", [owner](const Napi::CallbackInfo& info) {
            owner->Require(info.Env());
            const uint32_t channels = info[0].As<Napi::Number>().Uint32Value();
            const double requestedFrames = Number(info, 1);
            if (!std::isfinite(requestedFrames) || requestedFrames < 1 || requestedFrames > UINT32_MAX ||
                requestedFrames != std::floor(requestedFrames))
            {
                throw Napi::RangeError::New(info.Env(), "Native audio buffer length must be a positive integer.");
            }
            const auto frames = static_cast<size_t>(requestedFrames);
            const double rate = Number(info, 2);
            bl_HostAudioBuffer handle{};
            Check(info.Env(), owner->service.createBuffer(owner->service.userData, owner->context, channels, frames, rate, &handle));
            auto buffer = std::make_shared<Impl::Buffer>();
            buffer->owner = owner;
            buffer->handle = handle;
            owner->buffers.insert(buffer.get());
            for (uint32_t channel = 0; channel < channels; ++channel)
            {
                auto data = Napi::Float32Array::New(info.Env(), frames);
                buffer->channels.push_back(Napi::Persistent(data));
            }
            auto result = Napi::Object::New(info.Env());
            auto* token = new std::shared_ptr<Impl::Buffer>(buffer);
            owner->bufferTokens.insert(token);
            result.Set("_hostAudioBuffer", Napi::External<std::shared_ptr<Impl::Buffer>>::New(info.Env(), token,
                                               [owner](Napi::Env, std::shared_ptr<Impl::Buffer>* finalized) {
                                                   owner->bufferTokens.erase(finalized);
                                                   delete finalized;
                                               }));
            result.Set("sampleRate", rate);
            result.Set("length", static_cast<double>(frames));
            result.Set("numberOfChannels", channels);
            Method(result, "getChannelData", [buffer](const Napi::CallbackInfo& info) -> Napi::Value {
                buffer->owner->Require(info.Env());
                const uint32_t channel = info[0].As<Napi::Number>().Uint32Value();
                if (channel >= buffer->channels.size())
                    throw Napi::RangeError::New(info.Env(), "Audio channel is out of range.");
                return buffer->channels[channel].Value();
            });
            return result;
        });
        return object;
    }
}
