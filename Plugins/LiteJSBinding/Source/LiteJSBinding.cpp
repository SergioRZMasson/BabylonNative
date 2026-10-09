#include <Babylon/Plugins/LiteJSBinding.h>
#include "AudioBinding.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Babylon::Plugins::LiteJSBinding
{
    namespace
    {
        enum class Kind
        {
            Engine,
            Scene,
            Node,
            Mesh,
            Camera,
            Material,
            Texture,
            AudioEngine,
            SoundSource,
            UiContext,
            UiElement,
            ArcCamera,
            StandardMaterial,
            Light,
            DirectionalLight,
            Control,
            Limits
        };

        struct Token
        {
            Kind kind;
            bl_SceneNode identity;
            bl_SceneNode nodeIdentity{};
            uint64_t uiContextId{};
        };

        struct State;
        void SurfaceCallback(State& state, Napi::Env env);

        struct ControlPredicates
        {
            State* owner{};
            Napi::FunctionReference pointerDown;
            Napi::FunctionReference externalDrag;
            Napi::FunctionReference pendingPick;
        };

        struct BeforeCallback
        {
            State* owner{};
            Napi::FunctionReference function;
            bl_SceneContext scene;
            bl_CallbackToken token{};
            bool disposeCallback{};
        };

        struct StartCallback
        {
            State* owner{};
            Napi::Promise::Deferred deferred;
            bool completed{};
            bool settled{};
            bl_Status status{BL_NOT_READY};
        };

        struct UiCallback
        {
            State* owner{};
            Napi::FunctionReference function;
            bl_UiElement element{};
            bl_UiListenerToken token{};
            bool removed{};
            uint64_t identity{};
            uint64_t contextId{};
        };

        struct UiListenerIdentity
        {
            uint64_t identity{};
        };

        struct State
        {
            HostOptions options;
            bool disposed{};
            std::weak_ptr<State> self;
            std::unordered_set<const Token*> knownTokens;
            std::map<std::pair<Kind, uint64_t>, Napi::ObjectReference> objects;
            std::map<uint64_t, Napi::ObjectReference> nodeObjects;
            std::map<uint64_t, Napi::ObjectReference> engineObjects;
            std::map<uint64_t, Napi::ObjectReference> registeredScenes;
            std::vector<bl_EngineContext> engines;
            std::vector<std::unique_ptr<BeforeCallback>> callbacks;
            std::vector<std::unique_ptr<StartCallback>> starts;
            std::vector<bl_UiContext> uiContexts;
            std::vector<std::unique_ptr<UiCallback>> uiCallbacks;
            std::unordered_set<const UiListenerIdentity*> knownUiListeners;
            uint64_t nextUiListener{1};
            std::map<std::pair<uint64_t, std::string>, Napi::Reference<Napi::Float32Array>> uniformViews;
            std::map<uint64_t, std::shared_ptr<AudioBinding>> audioBindings;
            std::map<uint64_t, std::vector<Napi::ObjectReference>> audioSources;
            Napi::Error callbackError;
            bool callbackCaptureFailed{};
            bool restoringJsException{};
            uint32_t createdTokens{};
            bool inNativeFrame{};
            uint32_t uiCallDepth{};
            FrameTimings frameTimings{};
            std::vector<Token> finalized;
            std::map<uint64_t, Kind> familyKinds;
            std::map<uint64_t, std::unique_ptr<ControlPredicates>> controlPredicates;

            void DrainFinalized(Napi::Env env)
            {
                if (inNativeFrame || uiCallDepth || disposed || !options.runtime)
                {
                    return;
                }
                auto work = std::move(finalized);
                finalized.clear();
                std::stable_sort(work.begin(), work.end(), [](const Token& a, const Token& b) {
                    return a.kind == Kind::Scene && b.kind != Kind::Scene;
                });
                for (size_t index = 0; index < work.size(); ++index)
                {
                    const auto& token = work[index];
                    bl_Status status = BL_OK;
                    if (token.kind == Kind::Node || token.kind == Kind::Camera || token.kind == Kind::Mesh ||
                        token.kind == Kind::ArcCamera || token.kind == Kind::Light ||
                        token.kind == Kind::DirectionalLight)
                    {
                        status = bl_disposeNode(token.nodeIdentity);
                    }
                    else if (token.kind == Kind::Material)
                    {
                        status = bl_disposeShaderMaterial({token.identity._runtime, token.identity._id});
                    }
                    else if (token.kind == Kind::StandardMaterial)
                    {
                        status = bl_disposeStandardMaterial({token.identity._runtime, token.identity._id});
                    }
                    else if (token.kind == Kind::Control)
                    {
                        status = bl_detachControl({token.identity._runtime, token.identity._id});
                        if (status == BL_OK || status == BL_DISPOSED)
                        {
                            controlPredicates.erase(token.identity._id);
                        }
                    }
                    else if (token.kind == Kind::Limits)
                    {
                        status = bl_removeCameraLimits({token.identity._runtime, token.identity._id});
                    }
                    else if (token.kind == Kind::Texture)
                    {
                        status = bl_disposeTexture2D({token.identity._runtime, token.identity._id});
                    }
                    else if (token.kind == Kind::Scene)
                    {
                        status = bl_disposeScene({token.identity._runtime, token.identity._id});
                    }
                    else if (token.kind == Kind::SoundSource)
                    {
                        status = bl_disposeSoundSource({token.identity._runtime, token.identity._id});
                    }
                    else if (token.kind == Kind::AudioEngine)
                    {
                        status = bl_disposeAudioEngine({token.identity._runtime, token.identity._id});
                        if (status == BL_OK || status == BL_DISPOSED || status == BL_INVALID_HANDLE)
                        {
                            const auto found = audioBindings.find(token.identity._id);
                            if (found != audioBindings.end())
                            {
                                found->second->Invalidate();
                                audioBindings.erase(found);
                            }
                            audioSources.erase(token.identity._id);
                        }
                    }
                    if (status == BL_BUSY)
                    {
                        finalized.push_back(token);
                    }
                    else if (status != BL_OK && status != BL_DISPOSED && status != BL_INVALID_HANDLE)
                    {
                        finalized.insert(finalized.end(), work.begin() + index, work.end());
                        Check(env, status);
                    }
                    else
                    {
                        familyKinds.erase(token.identity._id);
                    }
                }
                SurfaceCallback(*this, env);
            }

            void Prune()
            {
                std::erase_if(objects, [](const auto& entry) { return entry.second.Value().IsEmpty(); });
                std::erase_if(nodeObjects, [](const auto& entry) { return entry.second.Value().IsEmpty(); });
                std::erase_if(uniformViews, [](const auto& entry) { return entry.second.Value().IsEmpty(); });
            }

            void RefreshUniform(bl_ShaderMaterial material, const std::string& name)
            {
                const auto found = uniformViews.find({material._id, name});
                if (found == uniformViews.end() || found->second.Value().IsEmpty())
                {
                    return;
                }
                bl_ShaderUniformView values{};
                if (bl_getShaderUniform(material, {name.data(), name.size()}, &values) == BL_OK)
                {
                    auto array = found->second.Value();
                    if (array.ElementLength() == values.values.count)
                    {
                        std::memcpy(array.Data(), values.values.data, values.values.count * sizeof(float));
                    }
                }
            }

            void Check(Napi::Env env, bl_Status status)
            {
                if (status == BL_OK)
                {
                    return;
                }
                bl_Error error{};
                std::string message = "Babylon Lite status " + std::to_string(status);
                const bool nativeError = options.runtime &&
                    bl_getLastError(options.runtime, &error) == BL_OK && error.status == status;
                if (nativeError && error.message.length)
                {
                    message.append(": ").append(error.message.data, error.message.length);
                }
                auto exception = Napi::Error::New(env, message);
                exception.Set("status", static_cast<double>(status));
                exception.Set("code", nativeError ? static_cast<double>(error.sourceErrorCode) : 0.0);
                if (nativeError && error.operation.length)
                {
                    exception.Set("operation", Napi::String::New(env, error.operation.data, error.operation.length));
                }
                throw exception;
            }

            Token& Get(Napi::Value value)
            {
                if (disposed || !value.IsObject())
                {
                    throw Napi::TypeError::New(value.Env(), "Expected a live Babylon Lite identity.");
                }
                const auto field = value.As<Napi::Object>().Get("_blToken");
                if (!field.IsExternal())
                {
                    throw Napi::TypeError::New(value.Env(), "Expected a Babylon Lite native identity.");
                }
                auto* token = field.As<Napi::External<Token>>().Data();
                if (!knownTokens.contains(token))
                {
                    throw Napi::TypeError::New(value.Env(), "Identity belongs to another native binding.");
                }
                return *token;
            }

            template<typename Handle>
            Handle HandleOf(Napi::Value value, Kind kind)
            {
                const auto& token = Get(value);
                if (token.kind != kind)
                {
                    throw Napi::TypeError::New(value.Env(), "Babylon Lite identity has the wrong type.");
                }
                return {token.identity._runtime, token.identity._id};
            }

            bl_Light LightOf(Napi::Value value)
            {
                const auto& token = Get(value);
                bl_Light result{};
                if (token.kind == Kind::Light)
                {
                    Check(value.Env(), bl_hemisphericLightAsLight(
                        {token.identity._runtime, token.identity._id}, &result));
                }
                else if (token.kind == Kind::DirectionalLight)
                {
                    Check(value.Env(), bl_directionalLightAsLight(
                        {token.identity._runtime, token.identity._id}, &result));
                }
                else
                {
                    throw Napi::TypeError::New(value.Env(), "Expected a light identity.");
                }
                return result;
            }

            bl_SceneNode NodeOf(Napi::Value value)
            {
                if (value.IsNull() || value.IsUndefined())
                {
                    return {};
                }
                const auto& token = Get(value);
                bl_SceneNode result{};
                switch (token.kind)
                {
                    case Kind::Node:
                        return token.identity;
                    case Kind::Mesh:
                        Check(value.Env(), bl_meshNode({token.identity._runtime, token.identity._id}, &result));
                        return result;
                    case Kind::Camera:
                        Check(value.Env(), bl_cameraNode({token.identity._runtime, token.identity._id}, &result));
                        return result;
                    case Kind::ArcCamera:
                        Check(value.Env(), bl_arcRotateCameraNode({token.identity._runtime, token.identity._id}, &result));
                        return result;
                    case Kind::Light:
                    case Kind::DirectionalLight:
                    {
                        const auto light = LightOf(value);
                        Check(value.Env(), bl_lightNode(light, &result));
                        return result;
                    }
                    default:
                        throw Napi::TypeError::New(value.Env(), "Expected a scene-node identity.");
                }
            }

            template<typename Handle>
            Napi::Value Wrap(Napi::Env env, Handle handle, Kind kind)
            {
                if (!handle._id)
                {
                    return env.Null();
                }
                const auto key = std::pair{kind, handle._id};
                if (kind == Kind::Camera || kind == Kind::ArcCamera || kind == Kind::Material ||
                    kind == Kind::StandardMaterial || kind == Kind::Light ||
                    kind == Kind::DirectionalLight)
                {
                    familyKinds.insert_or_assign(handle._id, kind);
                }
                const auto found = objects.find(key);
                if (found != objects.end())
                {
                    auto previous = found->second.Value();
                    if (!previous.IsEmpty())
                    {
                        return previous;
                    }
                    objects.erase(found);
                }
                if (++createdTokens % 64 == 0)
                {
                    Prune();
                }
                auto token = std::make_unique<Token>(Token{kind, {handle._runtime, handle._id}, {}});
                auto object = Napi::Object::New(env);
                const auto owner = self;
                auto external = Napi::External<Token>::New(env, token.get(), [owner](Napi::Env, Token* value) {
                    if (const auto state = owner.lock())
                    {
                        state->knownTokens.erase(value);
                        if (!state->disposed && state->options.runtime &&
                            value->kind != Kind::UiContext && value->kind != Kind::UiElement)
                        {
                            try
                            {
                                state->finalized.push_back(*value);
                            }
                            catch (...)
                            {
                                state->callbackCaptureFailed = true;
                            }
                        }
                    }
                    delete value;
                });
                knownTokens.insert(token.release());
                object.DefineProperty(Napi::PropertyDescriptor::Value("_blToken", external));
                auto reference = Napi::Persistent(object);
                reference.Unref();
                objects.emplace(key, std::move(reference));
                if (kind == Kind::Node || kind == Kind::Mesh || kind == Kind::Camera ||
                    kind == Kind::ArcCamera || kind == Kind::Light ||
                    kind == Kind::DirectionalLight)
                {
                    const auto node = NodeOf(object);
                    external.Data()->nodeIdentity = node;
                    auto nodeReference = Napi::Persistent(object);
                    nodeReference.Unref();
                    nodeObjects.insert_or_assign(node._id, std::move(nodeReference));
                }
                return object;
            }

            Napi::Value WrapNode(Napi::Env env, bl_SceneNode node)
            {
                const auto existing = nodeObjects.find(node._id);
                if (existing != nodeObjects.end())
                {
                    auto previous = existing->second.Value();
                    if (!previous.IsEmpty())
                    {
                        return previous;
                    }
                    nodeObjects.erase(existing);
                }
                return Wrap(env, node, Kind::Node);
            }

            bl_Camera CameraFamily(Napi::Value value)
            {
                if (value.IsNull()) return {};
                const auto& token = Get(value);
                bl_Camera result{};
                if (token.kind == Kind::Camera)
                    Check(value.Env(), bl_freeCameraAsCamera({token.identity._runtime, token.identity._id}, &result));
                else if (token.kind == Kind::ArcCamera)
                    Check(value.Env(), bl_arcRotateCameraAsCamera({token.identity._runtime, token.identity._id}, &result));
                else
                    throw Napi::TypeError::New(value.Env(), "Expected a camera family identity.");
                return result;
            }

            bl_Material MaterialFamily(Napi::Value value)
            {
                if (value.IsNull()) return {};
                const auto& token = Get(value);
                bl_Material result{};
                if (token.kind == Kind::Material)
                    Check(value.Env(), bl_shaderMaterialAsMaterial({token.identity._runtime, token.identity._id}, &result));
                else if (token.kind == Kind::StandardMaterial)
                    Check(value.Env(), bl_standardMaterialAsMaterial({token.identity._runtime, token.identity._id}, &result));
                else
                    throw Napi::TypeError::New(value.Env(), "Expected a material family identity.");
                return result;
            }

            template<typename Handle>
            Napi::Value WrapFamily(Napi::Env env, Handle handle)
            {
                if (!handle._id) return env.Null();
                const auto kind = familyKinds.find(handle._id);
                if (kind == familyKinds.end())
                    throw Napi::TypeError::New(env, "Unissued binding family identity.");
                return Wrap(env, handle, kind->second);
            }
        };

        using Function = std::function<Napi::Value(State&, const Napi::CallbackInfo&)>;

        void Bind(Napi::Object api, const std::shared_ptr<State>& state, const char* name, Function function)
        {
            api.Set(name, Napi::Function::New(api.Env(), [state, name, function = std::move(function)](const Napi::CallbackInfo& info) {
                try
                {
                    if (state->disposed)
                    {
                        state->Check(info.Env(), BL_DISPOSED);
                    }
                    state->DrainFinalized(info.Env());
                    return function(*state, info);
                }
                catch (const Napi::Error& error)
                {
                    if (state->restoringJsException)
                    {
                        state->restoringJsException = false;
                    }
                    else
                    {
                        if (error.Get("operation").IsUndefined())
                        {
                            error.Set("operation", name);
                        }
                        const auto kind = error.Get("name").As<Napi::String>().Utf8Value();
                        if (error.Get("status").IsUndefined() &&
                            (kind == "TypeError" || kind == "RangeError"))
                        {
                            error.Set("status", static_cast<double>(BL_INVALID_ARGUMENT));
                            error.Set("code", 0.0);
                        }
                    }
                    error.ThrowAsJavaScriptException();
                }
                catch (const std::exception& error)
                {
                    auto exception = Napi::Error::New(info.Env(), error.what());
                    exception.Set("operation", name);
                    exception.Set("status", static_cast<double>(BL_HOST_ERROR));
                    exception.Set("code", 0.0);
                    exception.ThrowAsJavaScriptException();
                }
                return info.Env().Undefined(); }, name));
        }

        std::shared_ptr<State> GetState(Napi::Env env)
        {
            return *env.Global().Get("_liteNative").As<Napi::Object>().Get("_owner").As<Napi::External<std::shared_ptr<State>>>().Data();
        }

        bl_String String(const std::string& value)
        {
            return {value.data(), value.size()};
        }

        bl_OptionalNumber Optional(Napi::Object object, const char* name)
        {
            const auto value = object.Get(name);
            return value.IsUndefined() ? bl_OptionalNumber{} : bl_OptionalNumber{true, value.As<Napi::Number>().DoubleValue()};
        }

        bl_OptionalBool OptionalBool(Napi::Object object, const char* name)
        {
            const auto value = object.Get(name);
            return value.IsUndefined() ? BL_BOOL_DEFAULT : value.As<Napi::Boolean>().Value() ? BL_BOOL_TRUE
                                                                                             : BL_BOOL_FALSE;
        }

        Napi::Object Options(Napi::Value value)
        {
            return value.IsUndefined() || value.IsNull() ? Napi::Object::New(value.Env()) : value.As<Napi::Object>();
        }

        bl_Vec3 Vector(Napi::Value value)
        {
            const auto object = value.As<Napi::Object>();
            if (value.IsArray() || value.IsTypedArray())
            {
                return {object.Get(uint32_t{0}).As<Napi::Number>().DoubleValue(),
                    object.Get(uint32_t{1}).As<Napi::Number>().DoubleValue(),
                    object.Get(uint32_t{2}).As<Napi::Number>().DoubleValue()};
            }
            return {object.Get("x").As<Napi::Number>().DoubleValue(), object.Get("y").As<Napi::Number>().DoubleValue(),
                object.Get("z").As<Napi::Number>().DoubleValue()};
        }

        Napi::Object Vector(Napi::Env env, bl_Vec3 value)
        {
            auto result = Napi::Object::New(env);
            result.Set("x", value.x);
            result.Set("y", value.y);
            result.Set("z", value.z);
            return result;
        }

        bl_F32Span F32(Napi::Value value)
        {
            if (value.IsUndefined() || value.IsNull())
            {
                return {};
            }
            if (!value.IsTypedArray() || value.As<Napi::TypedArray>().TypedArrayType() != napi_float32_array)
            {
                throw Napi::TypeError::New(value.Env(), "Expected Float32Array.");
            }
            const auto array = value.As<Napi::Float32Array>();
            return {array.Data(), array.ElementLength()};
        }

        bl_U32Span U32(Napi::Value value)
        {
            if (!value.IsTypedArray() || value.As<Napi::TypedArray>().TypedArrayType() != napi_uint32_array)
            {
                throw Napi::TypeError::New(value.Env(), "Expected Uint32Array.");
            }
            const auto array = value.As<Napi::Uint32Array>();
            return {array.Data(), array.ElementLength()};
        }

        bl_Bytes Bytes(Napi::Value value)
        {
            if (!value.IsTypedArray() || value.As<Napi::TypedArray>().TypedArrayType() != napi_uint8_array)
            {
                throw Napi::TypeError::New(value.Env(), "Expected Uint8Array.");
            }
            const auto array = value.As<Napi::Uint8Array>();
            return {array.Data(), array.ElementLength()};
        }

        bl_BoxOptions BoxOptions(Napi::Value value)
        {
            if (value.IsNumber())
            {
                return {{true, value.As<Napi::Number>().DoubleValue()}, {}, {}, {}};
            }
            auto options = Options(value);
            return {Optional(options, "size"), Optional(options, "width"), Optional(options, "height"), Optional(options, "depth")};
        }

        bl_SphereOptions SphereOptions(Napi::Value value)
        {
            auto options = Options(value);
            return {Optional(options, "segments"), Optional(options, "diameter"), Optional(options, "diameterX"),
                Optional(options, "diameterY"), Optional(options, "diameterZ")};
        }

        bl_GroundOptions GroundOptions(Napi::Value value)
        {
            const auto options = Options(value);
            bl_GroundOptions result{};
            result.width = Optional(options, "width");
            result.height = Optional(options, "height");
            result.subdivisions = Optional(options, "subdivisions");
            const auto scale = options.Get("uvScale");
            if (!scale.IsNull() && !scale.IsUndefined())
            {
                const auto tuple = scale.As<Napi::Object>();
                if (tuple.Get("length").As<Napi::Number>().Uint32Value() != 2)
                {
                    throw Napi::TypeError::New(value.Env(), "Ground uvScale requires two numbers.");
                }
                result.hasUvScale = true;
                result.uvScale = {tuple.Get(uint32_t(0)).As<Napi::Number>().DoubleValue(),
                    tuple.Get(uint32_t(1)).As<Napi::Number>().DoubleValue()};
            }
            return result;
        }

        std::vector<double> Numbers(Napi::Value value)
        {
            if (value.IsUndefined())
            {
                return {};
            }
            if (value.IsNumber())
            {
                return {value.As<Napi::Number>().DoubleValue()};
            }
            auto object = value.As<Napi::Object>();
            const uint32_t count = object.Get("length").As<Napi::Number>().Uint32Value();
            std::vector<double> result(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                result[i] = object.Get(i).As<Napi::Number>().DoubleValue();
            }
            return result;
        }

        template<typename Array, typename T>
        Napi::Value CopyArray(Napi::Env env, const T* data, size_t count)
        {
            auto result = Array::New(env, count);
            if (count)
            {
                std::memcpy(result.Data(), data, count * sizeof(T));
            }
            return result;
        }

        Napi::Value Geometry(State& state, const Napi::CallbackInfo& info, unsigned kind)
        {
            bl_GeometryData data{};
            if (kind == 2)
            {
                const auto options = GroundOptions(info[0]);
                state.Check(info.Env(), bl_createFlatGroundData(state.options.runtime, &options, &data));
            }
            else if (kind == 1)
            {
                const auto options = SphereOptions(info[0]);
                state.Check(info.Env(), bl_createSphereData(state.options.runtime, &options, &data));
            }
            else
            {
                const auto options = BoxOptions(info[0]);
                state.Check(info.Env(), bl_createBoxData(state.options.runtime, &options, &data));
            }
            try
            {
                auto result = Napi::Object::New(info.Env());
                result.Set("positions", CopyArray<Napi::Float32Array>(info.Env(), data.positions, data.vertexCount * 3));
                result.Set("normals", CopyArray<Napi::Float32Array>(info.Env(), data.normals, data.vertexCount * 3));
                result.Set("uvs", CopyArray<Napi::Float32Array>(info.Env(), data.uvs, data.vertexCount * 2));
                result.Set("indices", CopyArray<Napi::Uint32Array>(info.Env(), data.indices, data.indexCount));
                state.Check(info.Env(), bl_freeGeometryData(state.options.runtime, &data));
                return result;
            }
            catch (...)
            {
                bl_freeGeometryData(state.options.runtime, &data);
                throw;
            }
        }

        bl_VertexSemantic Attribute(const std::string& name)
        {
            const std::map<std::string, bl_VertexSemantic> names{
                {"position", BL_ATTRIBUTE_POSITION},
                {"normal", BL_ATTRIBUTE_NORMAL},
                {"uv", BL_ATTRIBUTE_UV},
                {"uv2", BL_ATTRIBUTE_UV2},
                {"tangent", BL_ATTRIBUTE_TANGENT},
                {"color", BL_ATTRIBUTE_COLOR},
            };
            const auto found = names.find(name);
            if (found == names.end())
            {
                throw std::invalid_argument("Vertex attribute is outside this native contract: " + name);
            }
            return found->second;
        }

        bl_ShaderUniformType UniformType(const std::string& name)
        {
            const std::map<std::string, bl_ShaderUniformType> names{
                {"f32", BL_UNIFORM_F32},
                {"u32", BL_UNIFORM_U32},
                {"i32", BL_UNIFORM_I32},
                {"vec2<f32>", BL_UNIFORM_VEC2},
                {"vec3<f32>", BL_UNIFORM_VEC3},
                {"vec4<f32>", BL_UNIFORM_VEC4},
                {"mat4x4<f32>", BL_UNIFORM_MAT4},
            };
            const auto found = names.find(name);
            if (found == names.end())
            {
                throw std::invalid_argument("Unknown ShaderMaterial uniform type: " + name);
            }
            return found->second;
        }

        Napi::Value Material(State& state, const Napi::CallbackInfo& info)
        {
            auto source = info[0].As<Napi::Object>();
            std::deque<std::string> strings;
            const auto text = [&](Napi::Value value) {
                strings.push_back(value.IsUndefined() ? std::string{} : value.As<Napi::String>().Utf8Value());
                return String(strings.back());
            };
            bl_ShaderMaterialOptions options{};
            options.name = text(source.Get("name"));
            options.vertexSource = text(source.Get("vertexSource"));
            options.fragmentSource = text(source.Get("fragmentSource"));
            const auto attributes = source.Get("attributes").As<Napi::Array>();
            std::vector<bl_VertexSemantic> attributeValues;
            for (uint32_t i = 0; i < attributes.Length(); ++i)
            {
                attributeValues.push_back(Attribute(attributes.Get(i).As<Napi::String>().Utf8Value()));
            }
            std::vector<bl_ShaderUniformDecl> uniforms;
            std::deque<std::vector<double>> defaults;
            const auto uniformValue = source.Get("uniforms");
            if (!uniformValue.IsUndefined())
            {
                const auto entries = uniformValue.As<Napi::Array>();
                for (uint32_t i = 0; i < entries.Length(); ++i)
                {
                    const auto entry = entries.Get(i);
                    bl_ShaderUniformDecl uniform{};
                    if (entry.IsString())
                    {
                        uniform.name = text(entry);
                        uniform.system = true;
                    }
                    else
                    {
                        const auto declaration = entry.As<Napi::Object>();
                        uniform.name = text(declaration.Get("name"));
                        uniform.type = UniformType(declaration.Get("type").As<Napi::String>().Utf8Value());
                        defaults.push_back(Numbers(declaration.Get("defaultValue")));
                        uniform.defaultValue = {defaults.back().data(), defaults.back().size()};
                    }
                    uniforms.push_back(uniform);
                }
            }
            std::vector<bl_ShaderSamplerDecl> samplers;
            const auto samplerValue = source.Get("samplers");
            if (!samplerValue.IsUndefined())
            {
                const auto entries = samplerValue.As<Napi::Array>();
                for (uint32_t i = 0; i < entries.Length(); ++i)
                {
                    const auto entry = entries.Get(i);
                    if (entry.IsString())
                    {
                        samplers.push_back({text(entry)});
                    }
                    else
                    {
                        const auto declaration = entry.As<Napi::Object>();
                        if ((declaration.Has("sampleType") && declaration.Get("sampleType").As<Napi::String>().Utf8Value() != "float") ||
                            (declaration.Has("viewDimension") && declaration.Get("viewDimension").As<Napi::String>().Utf8Value() != "2d") ||
                            (declaration.Has("comparison") && declaration.Get("comparison").As<Napi::Boolean>().Value()))
                        {
                            throw std::invalid_argument("Shader sampler declaration is outside the native 2D color-texture contract.");
                        }
                        samplers.push_back({text(declaration.Get("name"))});
                    }
                }
            }
            std::vector<bl_ShaderDefine> defines;
            const auto defineValue = source.Get("defines");
            if (!defineValue.IsUndefined())
            {
                const auto map = defineValue.As<Napi::Object>();
                const auto keys = map.GetPropertyNames();
                for (uint32_t i = 0; i < keys.Length(); ++i)
                {
                    const auto key = keys.Get(i);
                    const auto value = map.Get(key);
                    const bool boolean = value.IsBoolean();
                    defines.push_back({text(key), boolean && value.As<Napi::Boolean>().Value(), boolean,
                        boolean ? 0 : value.As<Napi::Number>().DoubleValue()});
                }
            }
            for (const auto* unsupported : {"storageBuffers", "blend", "stencil"})
            {
                if (source.Has(unsupported) && !source.Get(unsupported).IsUndefined())
                {
                    throw std::invalid_argument(std::string("Unsupported ShaderMaterial option: ") + unsupported);
                }
            }
            options.attributes = attributeValues.data();
            options.attributeCount = attributeValues.size();
            options.uniforms = uniforms.data();
            options.uniformCount = uniforms.size();
            options.samplers = samplers.data();
            options.samplerCount = samplers.size();
            options.defines = defines.data();
            options.defineCount = defines.size();
            options.needAlphaBlending = OptionalBool(source, "needAlphaBlending");
            options.needAlphaTesting = OptionalBool(source, "needAlphaTesting");
            options.backFaceCulling = OptionalBool(source, "backFaceCulling");
            options.depthWrite = OptionalBool(source, "depthWrite");
            options.depthBias = Optional(source, "depthBias");
            options.depthBiasSlopeScale = Optional(source, "depthBiasSlopeScale");
            if (source.Has("blendMode"))
            {
                const auto mode = source.Get("blendMode").As<Napi::String>().Utf8Value();
                if (mode != "alpha" && mode != "additive")
                    throw std::invalid_argument("Unknown blendMode.");
                options.blendMode = mode == "additive" ? BL_BLEND_ADDITIVE : BL_BLEND_ALPHA;
            }
            if (source.Has("depthCompare"))
            {
                const std::map<std::string, bl_DepthCompare> values{
                    {"never", BL_COMPARE_NEVER},
                    {"less", BL_COMPARE_LESS},
                    {"equal", BL_COMPARE_EQUAL},
                    {"less-equal", BL_COMPARE_LESS_EQUAL},
                    {"greater", BL_COMPARE_GREATER},
                    {"not-equal", BL_COMPARE_NOT_EQUAL},
                    {"greater-equal", BL_COMPARE_GREATER_EQUAL},
                    {"always", BL_COMPARE_ALWAYS},
                };
                options.depthCompare = values.at(source.Get("depthCompare").As<Napi::String>().Utf8Value());
            }
            if (source.Has("topology"))
            {
                const std::map<std::string, bl_Topology> values{
                    {"triangle-list", BL_TOPOLOGY_TRIANGLE_LIST},
                    {"line-list", BL_TOPOLOGY_LINE_LIST},
                    {"point-list", BL_TOPOLOGY_POINT_LIST},
                };
                options.topology = values.at(source.Get("topology").As<Napi::String>().Utf8Value());
            }
            bl_ShaderMaterial material{};
            state.Check(info.Env(), bl_createShaderMaterial(state.options.runtime, &options, &material));
            return state.Wrap(info.Env(), material, Kind::Material);
        }

        void BeforeRender(void* userData, double deltaMs) noexcept
        {
            auto& callback = *static_cast<BeforeCallback*>(userData);
            auto& state = *callback.owner;
            if (!state.callbackError.IsEmpty() || state.callbackCaptureFailed)
            {
                return;
            }
            const auto begin = std::chrono::steady_clock::now();
            try
            {
                try
                {
                    if (callback.disposeCallback)
                    {
                        callback.function.Call({});
                    }
                    else
                    {
                        callback.function.Call({Napi::Number::New(callback.function.Env(), deltaMs)});
                        const auto flush = callback.function.Env().Global().Get("_liteFlushReferenceValues");
                        if (flush.IsFunction() && !callback.function.Env().IsExceptionPending())
                            flush.As<Napi::Function>().Call({});
                    }
                    if (callback.function.Env().IsExceptionPending())
                    {
                        state.callbackError = callback.function.Env().GetAndClearPendingException();
                    }
                }
                catch (const Napi::Error& error)
                {
                    state.callbackError = callback.function.Env().IsExceptionPending()
                                              ? callback.function.Env().GetAndClearPendingException()
                                              : error;
                }
                catch (const std::exception& error)
                {
                    state.callbackError = Napi::Error::New(callback.function.Env(), error.what());
                }
            }
            catch (...)
            {
                state.callbackCaptureFailed = true;
            }
            state.frameTimings.userCallbacksMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - begin).count();
        }

        void FirstFrame(void* userData, bl_Status status) noexcept
        {
            auto& callback = *static_cast<StartCallback*>(userData);
            callback.completed = true;
            callback.status = status;
        }

        void SceneDisposed(void* userData) noexcept
        {
            BeforeRender(userData, 0);
        }

        void UiEvent(void* userData, const bl_UiEvent* event) noexcept
        {
            auto& callback = *static_cast<UiCallback*>(userData);
            auto& state = *callback.owner;
            if (callback.removed || !state.callbackError.IsEmpty() || state.callbackCaptureFailed)
            {
                return;
            }
            try
            {
                try
                {
                    auto env = callback.function.Env();
                    auto value = Napi::Object::New(env);
                    value.Set("kind", static_cast<double>(event->kind));
                    const auto currentTarget = state.Wrap(env, event->currentTarget, Kind::UiElement);
                    state.Get(currentTarget).uiContextId = callback.contextId;
                    value.Set("currentTarget", currentTarget);
                    const auto target = state.Wrap(env, event->target, Kind::UiElement);
                    if (!target.IsNull())
                    {
                        state.Get(target).uiContextId = callback.contextId;
                    }
                    value.Set("target", target);
                    value.Set("x", event->position.x);
                    value.Set("y", event->position.y);
                    value.Set("button", event->button);
                    value.Set("key", static_cast<double>(event->key));
                    value.Set("modifiers", event->modifiers);
                    value.Set("value",
                              Napi::String::New(env, event->value.data ? event->value.data : "",
                                                event->value.length));
                    callback.function.Call({value});
                    if (env.IsExceptionPending())
                    {
                        state.callbackError = env.GetAndClearPendingException();
                    }
                }
                catch (const Napi::Error& error)
                {
                    auto env = callback.function.Env();
                    state.callbackError =
                        env.IsExceptionPending() ? env.GetAndClearPendingException() : error;
                }
                catch (const std::exception& error)
                {
                    state.callbackError = Napi::Error::New(callback.function.Env(), error.what());
                }
            }
            catch (...)
            {
                state.callbackCaptureFailed = true;
            }
        }

        void SurfaceCallback(State& state, Napi::Env env)
        {
            if (!state.callbackError.IsEmpty())
            {
                auto error = std::move(state.callbackError);
                state.callbackError = {};
                state.restoringJsException = true;
                throw error;
            }
            if (state.callbackCaptureFailed)
            {
                state.callbackCaptureFailed = false;
                throw Napi::Error::New(env, "Failed to retain a JavaScript callback exception.");
            }
        }

        void SettleStarts(State& state, Napi::Env env)
        {
            if (state.inNativeFrame || state.uiCallDepth)
            {
                return;
            }
            for (auto& callback : state.starts)
            {
                if (!callback->completed || callback->settled)
                {
                    continue;
                }
                callback->settled = true;
                if (!state.callbackError.IsEmpty())
                {
                    callback->deferred.Reject(state.callbackError.Value());
                }
                else if (callback->status == BL_OK && !state.callbackCaptureFailed)
                {
                    callback->deferred.Resolve(env.Undefined());
                }
                else
                {
                    const auto status = state.callbackCaptureFailed ? BL_HOST_ERROR : callback->status;
                    auto error = Napi::Error::New(env, "Engine first frame failed or was cancelled.");
                    error.Set("status", static_cast<double>(status));
                    error.Set("code", 0.0);
                    error.Set("operation", "startEngine");
                    callback->deferred.Reject(error.Value());
                }
            }
            std::erase_if(state.starts, [](const auto& callback) { return callback->settled; });
        }

        uint32_t UiUint(Napi::Value value)
        {
            if (!value.IsNumber())
            {
                throw Napi::TypeError::New(value.Env(), "UI count must be an unsigned integer.");
            }
            const double number = value.As<Napi::Number>().DoubleValue();
            if (!std::isfinite(number) || number < 0 || number > UINT32_MAX ||
                std::floor(number) != number)
            {
                throw Napi::RangeError::New(value.Env(), "UI count is outside the uint32 range.");
            }
            return static_cast<uint32_t>(number);
        }

        bl_Bytes UiBytes(Napi::Value value)
        {
            if (!value.IsTypedArray() ||
                (value.As<Napi::TypedArray>().TypedArrayType() != napi_uint8_array &&
                 value.As<Napi::TypedArray>().TypedArrayType() != napi_uint8_clamped_array))
            {
                throw Napi::TypeError::New(value.Env(),
                                           "UI bytes require a Uint8Array or Uint8ClampedArray.");
            }
            auto array = value.As<Napi::Uint8Array>();
            return {array.Data(), array.ByteLength()};
        }

        void BindUi(Napi::Object api, const std::shared_ptr<State>& state)
        {
            Bind(api, state, "createUiContext",
                 [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                     const auto source = i[1].As<Napi::Object>();
                     bl_UiContextOptions options{};
                     options.target = s.options.nativeEngine.target;
                     const auto first = UiUint(source.Get("firstViewId"));
                     const auto count = UiUint(source.Get("viewCount"));
                     if (first > UINT16_MAX || count > UINT16_MAX)
                     {
                         throw Napi::RangeError::New(i.Env(), "UI view range exceeds uint16.");
                     }
                     options.target.firstViewId = static_cast<uint16_t>(first);
                     options.target.viewCount = static_cast<uint16_t>(count);
                     options.densityRatio =
                         source.Get("densityRatio").As<Napi::Number>().DoubleValue();
                     bl_UiContext context{};
                     s.Check(i.Env(),
                             bl_createUiContext(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine),
                                                &options, &context));
                     s.uiContexts.push_back(context);
                     return s.Wrap(i.Env(), context, Kind::UiContext);
                 });
            Bind(api, state, "disposeUiContext", [](State& s, const Napi::CallbackInfo& i) {
                const auto context = s.HandleOf<bl_UiContext>(i[0], Kind::UiContext);
                s.Check(i.Env(), bl_disposeUiContext(context));
                std::erase_if(s.uiCallbacks, [context](const auto& callback) {
                    return callback->contextId == context._id;
                });
                std::erase_if(s.uiContexts,
                              [context](const auto& value) { return value._id == context._id; });
                return i.Env().Undefined();
            });
            Bind(api, state, "setUiViewport", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(), bl_setUiViewport(s.HandleOf<bl_UiContext>(i[0], Kind::UiContext),
                                                  UiUint(i[1]), UiUint(i[2]),
                                                  i[3].As<Napi::Number>().DoubleValue()));
                return i.Env().Undefined();
            });
            Bind(api, state, "getUiRoot", [](State& s, const Napi::CallbackInfo& i) {
                bl_UiElement element{};
                const auto context = s.HandleOf<bl_UiContext>(i[0], Kind::UiContext);
                s.Check(i.Env(), bl_getUiRoot(context, &element));
                const auto result = s.Wrap(i.Env(), element, Kind::UiElement);
                s.Get(result).uiContextId = context._id;
                return result;
            });
            Bind(api, state, "createUiElement", [](State& s, const Napi::CallbackInfo& i) {
                bl_UiElement element{};
                const auto tag = i[1].As<Napi::String>().Utf8Value();
                const auto context = s.HandleOf<bl_UiContext>(i[0], Kind::UiContext);
                s.Check(i.Env(), bl_createUiElement(context, String(tag), &element));
                const auto result = s.Wrap(i.Env(), element, Kind::UiElement);
                s.Get(result).uiContextId = context._id;
                return result;
            });
            Bind(api, state, "appendUiChild", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(), bl_appendUiChild(s.HandleOf<bl_UiElement>(i[0], Kind::UiElement),
                                                  s.HandleOf<bl_UiElement>(i[1], Kind::UiElement)));
                return i.Env().Undefined();
            });
            Bind(api, state, "removeUiChild", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(), bl_removeUiChild(s.HandleOf<bl_UiElement>(i[0], Kind::UiElement),
                                                  s.HandleOf<bl_UiElement>(i[1], Kind::UiElement)));
                return i.Env().Undefined();
            });
            Bind(api, state, "disposeUiElement", [](State& s, const Napi::CallbackInfo& i) {
                const auto element = s.HandleOf<bl_UiElement>(i[0], Kind::UiElement);
                s.Check(i.Env(), bl_disposeUiElement(element));
                std::erase_if(s.uiCallbacks, [element](const auto& callback) {
                    return callback->element._id == element._id;
                });
                return i.Env().Undefined();
            });
            Bind(api, state, "setUiProperty", [](State& s, const Napi::CallbackInfo& i) {
                const auto name = i[1].As<Napi::String>().Utf8Value();
                const auto value = i[2].As<Napi::String>().Utf8Value();
                s.Check(i.Env(), bl_setUiProperty(s.HandleOf<bl_UiElement>(i[0], Kind::UiElement),
                                                  String(name), String(value)));
                return i.Env().Undefined();
            });
            Bind(api, state, "removeUiProperty", [](State& s, const Napi::CallbackInfo& i) {
                const auto name = i[1].As<Napi::String>().Utf8Value();
                s.Check(i.Env(),
                        bl_removeUiProperty(s.HandleOf<bl_UiElement>(i[0], Kind::UiElement),
                                            String(name)));
                return i.Env().Undefined();
            });
            Bind(api, state, "setUiAttribute", [](State& s, const Napi::CallbackInfo& i) {
                const auto name = i[1].As<Napi::String>().Utf8Value();
                const auto value = i[2].As<Napi::String>().Utf8Value();
                s.Check(i.Env(), bl_setUiAttribute(s.HandleOf<bl_UiElement>(i[0], Kind::UiElement),
                                                   String(name), String(value)));
                return i.Env().Undefined();
            });
            Bind(api, state, "setUiText", [](State& s, const Napi::CallbackInfo& i) {
                const auto text = i[1].As<Napi::String>().Utf8Value();
                s.Check(i.Env(), bl_setUiText(s.HandleOf<bl_UiElement>(i[0], Kind::UiElement),
                                              String(text)));
                return i.Env().Undefined();
            });
            Bind(api, state, "setUiMarkup", [](State& s, const Napi::CallbackInfo& i) {
                const auto text = i[1].As<Napi::String>().Utf8Value();
                s.Check(i.Env(), bl_setUiMarkup(s.HandleOf<bl_UiElement>(i[0], Kind::UiElement),
                                                String(text)));
                return i.Env().Undefined();
            });
            Bind(api, state, "loadUiFont", [](State& s, const Napi::CallbackInfo& i) {
                const auto family = i[2].As<Napi::String>().Utf8Value();
                s.Check(i.Env(), bl_loadUiFont(s.HandleOf<bl_UiContext>(i[0], Kind::UiContext),
                                               UiBytes(i[1]), String(family), UiUint(i[3]),
                                               i[4].As<Napi::Boolean>().Value(),
                                               i[5].As<Napi::Boolean>().Value()));
                return i.Env().Undefined();
            });
            Bind(api, state, "registerUiImage", [](State& s, const Napi::CallbackInfo& i) {
                const auto source = i[1].As<Napi::String>().Utf8Value();
                s.Check(i.Env(), bl_registerUiImage(s.HandleOf<bl_UiContext>(i[0], Kind::UiContext),
                                                    String(source), UiBytes(i[2]), UiUint(i[3]),
                                                    UiUint(i[4])));
                return i.Env().Undefined();
            });
            Bind(api, state, "unregisterUiImage", [](State& s, const Napi::CallbackInfo& i) {
                const auto source = i[1].As<Napi::String>().Utf8Value();
                s.Check(i.Env(),
                        bl_unregisterUiImage(s.HandleOf<bl_UiContext>(i[0], Kind::UiContext),
                                             String(source)));
                return i.Env().Undefined();
            });
            Bind(api, state, "setUiImageSampling", [](State& s, const Napi::CallbackInfo& i) {
                const auto source = i[1].As<Napi::String>().Utf8Value();
                s.Check(i.Env(),
                        bl_setUiImageSampling(s.HandleOf<bl_UiContext>(i[0], Kind::UiContext),
                                              String(source), i[2].As<Napi::Boolean>().Value()));
                return i.Env().Undefined();
            });
            Bind(api, state, "setUiWhiteDifference", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(),
                        bl_setUiWhiteDifference(s.HandleOf<bl_UiContext>(i[0], Kind::UiContext),
                                                i[1].As<Napi::Boolean>().Value()));
                return i.Env().Undefined();
            });
            Bind(api, state, "updateUi", [](State& s, const Napi::CallbackInfo& i) {
                const auto context = s.HandleOf<bl_UiContext>(i[0], Kind::UiContext);
                const auto seconds = i[1].As<Napi::Number>().DoubleValue();
                ++s.uiCallDepth;
                const auto status = bl_updateUi(context, seconds);
                --s.uiCallDepth;
                SettleStarts(s, i.Env());
                SurfaceCallback(s, i.Env());
                s.Check(i.Env(), status);
                return i.Env().Undefined();
            });
            Bind(api, state, "renderUi", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(), bl_renderUi(s.HandleOf<bl_UiContext>(i[0], Kind::UiContext)));
                return i.Env().Undefined();
            });
            Bind(api, state, "getUiStats", [](State& s, const Napi::CallbackInfo& i) {
                bl_UiStats stats{};
                s.Check(i.Env(),
                        bl_getUiStats(s.HandleOf<bl_UiContext>(i[0], Kind::UiContext), &stats));
                auto result = Napi::Object::New(i.Env());
                result.Set("geometryCompileCount", static_cast<double>(stats.geometryCompileCount));
                result.Set("geometryReleaseCount", static_cast<double>(stats.geometryReleaseCount));
                result.Set("textureCreateCount", static_cast<double>(stats.textureCreateCount));
                result.Set("textureReleaseCount", static_cast<double>(stats.textureReleaseCount));
                result.Set("drawCount", static_cast<double>(stats.drawCount));
                result.Set("uploadedBytes", static_cast<double>(stats.uploadedBytes));
                result.Set("liveGeometryCount", static_cast<double>(stats.liveGeometryCount));
                result.Set("liveTextureCount", static_cast<double>(stats.liveTextureCount));
                result.Set("liveElementCount", static_cast<double>(stats.liveElementCount));
                return result;
            });
            Bind(api, state, "processUiInput", [](State& s, const Napi::CallbackInfo& i) {
                const auto source = i[1].As<Napi::Object>();
                bl_UiInput input{};
                input.kind = static_cast<bl_UiInputKind>(UiUint(source.Get("kind")));
                input.x = source.Get("x").As<Napi::Number>().DoubleValue();
                input.y = source.Get("y").As<Napi::Number>().DoubleValue();
                input.button = UiUint(source.Get("button"));
                input.key = static_cast<bl_UiKey>(UiUint(source.Get("key")));
                input.modifiers = UiUint(source.Get("modifiers"));
                const auto text = source.Get("text").As<Napi::String>().Utf8Value();
                input.text = String(text);
                bool consumed{};
                const auto context = s.HandleOf<bl_UiContext>(i[0], Kind::UiContext);
                ++s.uiCallDepth;
                const auto status = bl_processUiInput(context, &input, &consumed);
                --s.uiCallDepth;
                SettleStarts(s, i.Env());
                SurfaceCallback(s, i.Env());
                s.Check(i.Env(), status);
                return Napi::Boolean::New(i.Env(), consumed);
            });
            Bind(api, state, "addUiEventListener", [](State& s, const Napi::CallbackInfo& i) {
                if (s.uiCallbacks.size() >= 4096 || !s.nextUiListener)
                {
                    throw Napi::RangeError::New(i.Env(),
                                                "UI active listener budget (4096) exceeded.");
                }
                auto callback = std::make_unique<UiCallback>();
                callback->owner = &s;
                callback->element = s.HandleOf<bl_UiElement>(i[0], Kind::UiElement);
                callback->contextId = s.Get(i[0]).uiContextId;
                callback->identity = s.nextUiListener++;
                callback->function = Napi::Persistent(i[2].As<Napi::Function>());
                auto* pointer = callback.get();
                s.uiCallbacks.push_back(std::move(callback));
                const auto status = bl_addUiEventListener(pointer->element,
                                                          static_cast<bl_UiEventKind>(UiUint(i[1])),
                                                          UiEvent, pointer, &pointer->token);
                if (status != BL_OK)
                {
                    s.uiCallbacks.pop_back();
                    s.Check(i.Env(), status);
                }
                auto result = Napi::Object::New(i.Env());
                auto identity = std::make_unique<UiListenerIdentity>();
                identity->identity = pointer->identity;
                const auto owner = s.self;
                auto external = Napi::External<UiListenerIdentity>::New(
                    i.Env(), identity.get(), [owner](Napi::Env, UiListenerIdentity* value) {
                        if (const auto state = owner.lock())
                        {
                            state->knownUiListeners.erase(value);
                        }
                        delete value;
                    });
                s.knownUiListeners.insert(identity.release());
                result.Set("_listener", external);
                return result;
            });
            Bind(api, state, "removeUiEventListener", [](State& s, const Napi::CallbackInfo& i) {
                const auto field = i[1].As<Napi::Object>().Get("_listener");
                if (!field.IsExternal())
                {
                    throw Napi::TypeError::New(i.Env(), "Expected a UI listener token.");
                }
                auto* identity = field.As<Napi::External<UiListenerIdentity>>().Data();
                if (!s.knownUiListeners.contains(identity))
                {
                    throw Napi::TypeError::New(i.Env(),
                                               "UI listener token belongs to another binding.");
                }
                const auto found = std::find_if(s.uiCallbacks.begin(), s.uiCallbacks.end(),
                                                [identity](const auto& entry) {
                                                    return entry->identity == identity->identity;
                                                });
                if (found == s.uiCallbacks.end())
                {
                    throw Napi::TypeError::New(i.Env(), "Unknown or removed UI listener token.");
                }
                auto* callback = found->get();
                s.Check(i.Env(),
                        bl_removeUiEventListener(s.HandleOf<bl_UiElement>(i[0], Kind::UiElement),
                                                 callback->token));
                s.uiCallbacks.erase(found);
                return i.Env().Undefined();
            });
        }

        bl_MeshGeometry MeshGeometry(Napi::Value value)
        {
            const auto source = value.As<Napi::Object>();
            return {F32(source.Get("positions")), F32(source.Get("normals")),
                    U32(source.Get("indices")),   F32(source.Get("uvs")),
                    F32(source.Get("uvs2")),      F32(source.Get("tangents")),
                    F32(source.Get("colors"))};
        }

        bl_RampOptions Ramp(Napi::Value value)
        {
            const auto source = Options(value);
            bl_RampOptions options{};
            options.duration = Optional(source, "duration");
            if (source.Has("shape"))
            {
                const std::map<std::string, bl_AudioRampShape> shapes{
                    {"linear", BL_AUDIO_RAMP_LINEAR},
                    {"none", BL_AUDIO_RAMP_NONE},
                    {"exponential", BL_AUDIO_RAMP_EXPONENTIAL},
                    {"logarithmic", BL_AUDIO_RAMP_LOGARITHMIC}};
                options.shape = shapes.at(source.Get("shape").As<Napi::String>().Utf8Value());
            }
            return options;
        }

        Napi::Object SceneCallbackIdentity(Napi::Env env, Napi::Value scene, bl_CallbackToken token)
        {
            auto result = Napi::Object::New(env);
            result.Set("_scene", scene);
            result.Set("_callback", std::to_string(token.value));
            return result;
        }

        Napi::Object VectorObject(Napi::Env env, bl_Vec3 v)
        {
            auto result = Napi::Object::New(env);
            result.Set("x", v.x);
            result.Set("y", v.y);
            result.Set("z", v.z);
            return result;
        }

        Napi::Array ColorArray(Napi::Env env, bl_Vec3 v)
        {
            auto result = Napi::Array::New(env, 3);
            result.Set(uint32_t{0}, v.x);
            result.Set(uint32_t{1}, v.y);
            result.Set(uint32_t{2}, v.z);
            return result;
        }

        bl_Vec3 ColorValue(Napi::Value value)
        {
            const auto numbers = Numbers(value);
            if (numbers.size() != 3)
                throw Napi::TypeError::New(value.Env(), "Expected exactly three RGB/direction values.");
            return {numbers[0], numbers[1], numbers[2]};
        }

        const std::map<std::string, double bl_ArcRotateCameraProperties::*> ArcNumbers{
            {"alpha", &bl_ArcRotateCameraProperties::alpha},
            {"beta", &bl_ArcRotateCameraProperties::beta},
            {"radius", &bl_ArcRotateCameraProperties::radius},
            {"fov", &bl_ArcRotateCameraProperties::fov},
            {"nearPlane", &bl_ArcRotateCameraProperties::nearPlane},
            {"farPlane", &bl_ArcRotateCameraProperties::farPlane},
            {"inertia", &bl_ArcRotateCameraProperties::inertia},
            {"panningInertia", &bl_ArcRotateCameraProperties::panningInertia},
            {"angularSensibility", &bl_ArcRotateCameraProperties::angularSensibility},
            {"panningSensibility", &bl_ArcRotateCameraProperties::panningSensibility},
            {"wheelPrecision", &bl_ArcRotateCameraProperties::wheelPrecision},
            {"inertialAlphaOffset", &bl_ArcRotateCameraProperties::inertialAlphaOffset},
            {"inertialBetaOffset", &bl_ArcRotateCameraProperties::inertialBetaOffset},
            {"inertialRadiusOffset", &bl_ArcRotateCameraProperties::inertialRadiusOffset},
            {"inertialPanningX", &bl_ArcRotateCameraProperties::inertialPanningX},
            {"inertialPanningY", &bl_ArcRotateCameraProperties::inertialPanningY}};

        const std::map<std::string, bl_OptionalNumber bl_ArcRotateCameraLimits::*> LimitFields{
            {"lowerAlphaLimit", &bl_ArcRotateCameraLimits::lowerAlphaLimit},
            {"upperAlphaLimit", &bl_ArcRotateCameraLimits::upperAlphaLimit},
            {"lowerBetaLimit", &bl_ArcRotateCameraLimits::lowerBetaLimit},
            {"upperBetaLimit", &bl_ArcRotateCameraLimits::upperBetaLimit},
            {"lowerRadiusLimit", &bl_ArcRotateCameraLimits::lowerRadiusLimit},
            {"upperRadiusLimit", &bl_ArcRotateCameraLimits::upperRadiusLimit}};

        const std::map<std::string, bl_Vec3 bl_StandardMaterialProperties::*> StandardColors{
            {"diffuseColor", &bl_StandardMaterialProperties::diffuseColor},
            {"specularColor", &bl_StandardMaterialProperties::specularColor},
            {"emissiveColor", &bl_StandardMaterialProperties::emissiveColor},
            {"ambientColor", &bl_StandardMaterialProperties::ambientColor}};

        const std::map<std::string, bl_Vec3 bl_HemisphericLightProperties::*> LightColors{
            {"diffuseColor", &bl_HemisphericLightProperties::diffuseColor},
            {"specularColor", &bl_HemisphericLightProperties::specularColor},
            {"groundColor", &bl_HemisphericLightProperties::groundColor}};

        const std::map<std::string, bl_Vec3 bl_DirectionalLightProperties::*> DirectionalColors{
            {"diffuse", &bl_DirectionalLightProperties::diffuse},
            {"specular", &bl_DirectionalLightProperties::specular}};

        Napi::Object PointerObject(Napi::Env env, const bl_ArcRotateInput& input)
        {
            auto object = Napi::Object::New(env);
            object.Set("clientX", input.clientX);
            object.Set("clientY", input.clientY);
            object.Set("button", input.button);
            object.Set("pointerId", static_cast<double>(input.pointerId));
            object.Set("pointerType", input.pointerType == BL_ARC_POINTER_TOUCH ? "touch" :
                                      input.pointerType == BL_ARC_POINTER_PEN ? "pen" : "mouse");
            return object;
        }

        bool ControlPredicate(ControlPredicates& callbacks, Napi::FunctionReference& function,
                              const bl_ArcRotateInput* input) noexcept
        {
            if (function.IsEmpty()) return false;
            auto& state = *callbacks.owner;
            if (!state.callbackError.IsEmpty() || state.callbackCaptureFailed) return false;
            bool result = false;
            try
            {
                try
                {
                    const auto value = input
                        ? function.Call({PointerObject(function.Env(), *input)})
                        : function.Call({});
                    if (function.Env().IsExceptionPending())
                        state.callbackError = function.Env().GetAndClearPendingException();
                    else
                        result = value.ToBoolean().Value();
                }
                catch (const Napi::Error& error)
                {
                    state.callbackError = function.Env().IsExceptionPending()
                        ? function.Env().GetAndClearPendingException() : error;
                }
                catch (const std::exception& error)
                {
                    state.callbackError = Napi::Error::New(function.Env(), error.what());
                }
            }
            catch (...)
            {
                state.callbackCaptureFailed = true;
            }
            return result;
        }

        void BindFamilies(Napi::Object api, const std::shared_ptr<State>& state)
        {
            Bind(api, state, "setArcRotateCameraLimitFields", [](State& s, const Napi::CallbackInfo& i) {
                const auto source = Options(i[1]);
                bl_ArcRotateCameraLimitPatch patch{};
                const std::map<std::string, uint32_t> masks{
                    {"lowerAlphaLimit", BL_LIMIT_LOWER_ALPHA}, {"upperAlphaLimit", BL_LIMIT_UPPER_ALPHA},
                    {"lowerBetaLimit", BL_LIMIT_LOWER_BETA}, {"upperBetaLimit", BL_LIMIT_UPPER_BETA},
                    {"lowerRadiusLimit", BL_LIMIT_LOWER_RADIUS}, {"upperRadiusLimit", BL_LIMIT_UPPER_RADIUS}};
                for (const auto& [name, member] : LimitFields)
                {
                    if (source.HasOwnProperty(name))
                    {
                        patch.fields |= masks.at(name);
                        patch.values.*member = Optional(source, name.c_str());
                    }
                }
                s.Check(i.Env(), bl_setArcRotateCameraLimitFields(
                    s.HandleOf<bl_ArcRotateCamera>(i[0], Kind::ArcCamera), &patch));
                return i.Env().Undefined();
            });
            Bind(api, state, "setArcRotateControlOptions", [](State& s, const Napi::CallbackInfo& i) {
                const auto control = s.HandleOf<bl_ArcRotateControl>(i[0], Kind::Control);
                if (i[1].IsNull() || i[1].IsUndefined())
                {
                    s.Check(i.Env(), bl_setArcRotateControlOptions(control, nullptr));
                    return i.Env().Undefined();
                }
                const auto source = Options(i[1]);
                bl_ArcRotateControlOptions options{};
                options.keyboard = source.Get("keyboard").ToBoolean().Value();
                if (source.Has("pointerMappings") && !source.Get("pointerMappings").IsUndefined())
                {
                    const auto mappings = source.Get("pointerMappings").As<Napi::Object>();
                    const auto action = [&](const char* name) {
                        const auto value = mappings.Get(name);
                        if (value.IsUndefined()) return BL_ARC_ACTION_DEFAULT;
                        const auto text = value.As<Napi::String>().Utf8Value();
                        if (text == "rotate") return BL_ARC_ACTION_ROTATE;
                        if (text == "pan") return BL_ARC_ACTION_PAN;
                        throw Napi::TypeError::New(i.Env(), "Unknown ArcRotate pointer action.");
                    };
                    options.primaryButton = action("primaryButton");
                    options.secondaryButton = action("secondaryButton");
                }
                auto callbacks = std::make_unique<ControlPredicates>();
                callbacks->owner = &s;
                if (source.Get("shouldHandlePointerDown").IsFunction())
                {
                    callbacks->pointerDown = Napi::Persistent(source.Get("shouldHandlePointerDown").As<Napi::Function>());
                    options.shouldHandlePointerDown = [](void* user, const bl_ArcRotateInput* input) noexcept {
                        auto& c = *static_cast<ControlPredicates*>(user);
                        return ControlPredicate(c, c.pointerDown, input);
                    };
                }
                if (source.Get("isExternalDragActive").IsFunction())
                {
                    callbacks->externalDrag = Napi::Persistent(source.Get("isExternalDragActive").As<Napi::Function>());
                    options.isExternalDragActive = [](void* user) noexcept {
                        auto& c = *static_cast<ControlPredicates*>(user);
                        return ControlPredicate(c, c.externalDrag, nullptr);
                    };
                }
                if (source.Get("isExternalPickPending").IsFunction())
                {
                    callbacks->pendingPick = Napi::Persistent(source.Get("isExternalPickPending").As<Napi::Function>());
                    options.isExternalPickPending = [](void* user) noexcept {
                        auto& c = *static_cast<ControlPredicates*>(user);
                        return ControlPredicate(c, c.pendingPick, nullptr);
                    };
                }
                const auto found = s.controlPredicates.find(control._id);
                if (found == s.controlPredicates.end())
                    throw Napi::TypeError::New(i.Env(), "Unknown native control callbacks.");
                options.userData = callbacks.get();
                s.Check(i.Env(), bl_setArcRotateControlOptions(control, &options));
                found->second = std::move(callbacks);
                return i.Env().Undefined();
            });
            Bind(api, state, "createArcRotateCamera", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_ArcRotateCamera camera{};
                s.Check(i.Env(), bl_createArcRotateCamera(s.options.runtime,
                    i[0].As<Napi::Number>().DoubleValue(), i[1].As<Napi::Number>().DoubleValue(),
                    i[2].As<Napi::Number>().DoubleValue(), Vector(i[3]), &camera));
                return s.Wrap(i.Env(), camera, Kind::ArcCamera);
            });
            Bind(api, state, "getArcRotateCameraProperties", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_ArcRotateCameraProperties p{};
                s.Check(i.Env(), bl_getArcRotateCameraProperties(s.HandleOf<bl_ArcRotateCamera>(i[0], Kind::ArcCamera), &p));
                auto value = Napi::Object::New(i.Env());
                for (const auto& [name, member] : ArcNumbers) value.Set(name, p.*member);
                value.Set("target", VectorObject(i.Env(), p.target));
                return value;
            });
            Bind(api, state, "setArcRotateCameraProperty", [](State& s, const Napi::CallbackInfo& i) {
                const auto camera = s.HandleOf<bl_ArcRotateCamera>(i[0], Kind::ArcCamera);
                bl_ArcRotateCameraProperties p{};
                s.Check(i.Env(), bl_getArcRotateCameraProperties(camera, &p));
                const auto name = i[1].As<Napi::String>().Utf8Value();
                if (name == "target") p.target = Vector(i[2]);
                else if (const auto member = ArcNumbers.find(name); member != ArcNumbers.end())
                    p.*member->second = i[2].As<Napi::Number>().DoubleValue();
                else throw Napi::TypeError::New(i.Env(), "Unsupported ArcRotate property.");
                s.Check(i.Env(), bl_setArcRotateCameraProperties(camera, &p));
                return i.Env().Undefined();
            });
            Bind(api, state, "getArcRotateCameraLimits", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_ArcRotateCameraLimits limits{};
                bool enforced{};
                s.Check(i.Env(), bl_getArcRotateCameraLimits(s.HandleOf<bl_ArcRotateCamera>(i[0], Kind::ArcCamera), &limits, &enforced));
                auto value = Napi::Object::New(i.Env());
                for (const auto& [name, member] : LimitFields)
                    value.Set(name, (limits.*member).present
                        ? Napi::Value(Napi::Number::New(i.Env(), (limits.*member).value)) : i.Env().Undefined());
                value.Set("enforced", enforced);
                return value;
            });
            Bind(api, state, "setCameraLimits", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                const auto source = Options(i[1]);
                bl_ArcRotateCameraLimitPatch patch{};
                const std::map<std::string, uint32_t> masks{
                    {"lowerAlphaLimit", BL_LIMIT_LOWER_ALPHA}, {"upperAlphaLimit", BL_LIMIT_UPPER_ALPHA},
                    {"lowerBetaLimit", BL_LIMIT_LOWER_BETA}, {"upperBetaLimit", BL_LIMIT_UPPER_BETA},
                    {"lowerRadiusLimit", BL_LIMIT_LOWER_RADIUS}, {"upperRadiusLimit", BL_LIMIT_UPPER_RADIUS}};
                for (const auto& [name, member] : LimitFields)
                {
                    if (source.HasOwnProperty(name))
                    {
                        patch.fields |= masks.at(name);
                        patch.values.*member = Optional(source, name.c_str());
                    }
                }
                bl_CameraLimitToken disposer{};
                s.Check(i.Env(), bl_setCameraLimits(s.HandleOf<bl_ArcRotateCamera>(i[0], Kind::ArcCamera), &patch, &disposer));
                return s.Wrap(i.Env(), disposer, Kind::Limits);
            });
            Bind(api, state, "removeCameraLimits", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(), bl_removeCameraLimits(s.HandleOf<bl_CameraLimitToken>(i[0], Kind::Limits)));
                return i.Env().Undefined();
            });
            Bind(api, state, "attachControl", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                const auto source = Options(i[2]);
                bl_ArcRotateControlOptions options{};
                if (source.Has("keyboard") && !source.Get("keyboard").IsUndefined())
                    options.keyboard = source.Get("keyboard").ToBoolean().Value();
                if (source.Has("pointerMappings"))
                {
                    const auto mappings = source.Get("pointerMappings").As<Napi::Object>();
                    const std::map<std::string, bl_ArcRotatePointerAction> actions{
                        {"rotate", BL_ARC_ACTION_ROTATE}, {"pan", BL_ARC_ACTION_PAN}};
                    const auto action = [&](const char* name) {
                        if (!mappings.Has(name) || mappings.Get(name).IsUndefined())
                            return BL_ARC_ACTION_DEFAULT;
                        const auto found = actions.find(mappings.Get(name).As<Napi::String>().Utf8Value());
                        if (found == actions.end())
                            throw Napi::TypeError::New(i.Env(), "Unknown ArcRotate pointer action.");
                        return found->second;
                    };
                    options.primaryButton = action("primaryButton");
                    options.secondaryButton = action("secondaryButton");
                }
                auto callbacks = std::make_unique<ControlPredicates>();
                callbacks->owner = &s;
                if (source.Get("shouldHandlePointerDown").IsFunction())
                {
                    callbacks->pointerDown = Napi::Persistent(source.Get("shouldHandlePointerDown").As<Napi::Function>());
                    options.shouldHandlePointerDown = [](void* user, const bl_ArcRotateInput* input) noexcept {
                        auto& callbacks = *static_cast<ControlPredicates*>(user);
                        return ControlPredicate(callbacks, callbacks.pointerDown, input);
                    };
                }
                if (source.Get("isExternalDragActive").IsFunction())
                {
                    callbacks->externalDrag = Napi::Persistent(source.Get("isExternalDragActive").As<Napi::Function>());
                    options.isExternalDragActive = [](void* user) noexcept {
                        auto& callbacks = *static_cast<ControlPredicates*>(user);
                        return ControlPredicate(callbacks, callbacks.externalDrag, nullptr);
                    };
                }
                if (source.Get("isExternalPickPending").IsFunction())
                {
                    callbacks->pendingPick = Napi::Persistent(source.Get("isExternalPickPending").As<Napi::Function>());
                    options.isExternalPickPending = [](void* user) noexcept {
                        auto& callbacks = *static_cast<ControlPredicates*>(user);
                        return ControlPredicate(callbacks, callbacks.pendingPick, nullptr);
                    };
                }
                options.userData = callbacks.get();
                const auto camera = s.HandleOf<bl_ArcRotateCamera>(i[0], Kind::ArcCamera);
                const auto scene = i[1].IsNull() || i[1].IsUndefined() ? bl_SceneContext{}
                    : s.HandleOf<bl_SceneContext>(i[1], Kind::Scene);
                auto ownedCallbacks = s.controlPredicates.emplace(0, std::move(callbacks));
                if (!ownedCallbacks.second)
                    throw Napi::Error::New(i.Env(), "Nested native control attachment.");
                bl_ArcRotateControl control{};
                const auto status = bl_attachControl(camera, scene, &options, &control);
                if (status != BL_OK)
                {
                    s.controlPredicates.erase(0);
                    s.Check(i.Env(), status);
                }
                auto entry = s.controlPredicates.extract(0);
                entry.key() = control._id;
                s.controlPredicates.insert(std::move(entry));
                try
                {
                    return s.Wrap(i.Env(), control, Kind::Control);
                }
                catch (...)
                {
                    const auto detached = bl_detachControl(control);
                    if (detached == BL_OK)
                        s.controlPredicates.erase(control._id);
                    throw;
                }
            });
            Bind(api, state, "processArcRotateInput", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                const auto source = i[1].As<Napi::Object>();
                bl_ArcRotateInput input{};
                const std::map<std::string, bl_ArcRotateInputKind> kinds{
                    {"pointerdown", BL_ARC_POINTER_DOWN}, {"pointermove", BL_ARC_POINTER_MOVE},
                    {"pointerup", BL_ARC_POINTER_UP}, {"wheel", BL_ARC_WHEEL},
                    {"touchstart", BL_ARC_TOUCH_START}, {"touchmove", BL_ARC_TOUCH_MOVE},
                    {"touchend", BL_ARC_TOUCH_END}, {"contextmenu", BL_ARC_CONTEXT_MENU},
                    {"gesturestart", BL_ARC_GESTURE}, {"gesturechange", BL_ARC_GESTURE},
                    {"gestureend", BL_ARC_GESTURE}};
                const auto kind = kinds.find(source.Get("type").As<Napi::String>().Utf8Value());
                if (kind == kinds.end())
                    throw Napi::TypeError::New(i.Env(), "Unknown ArcRotate input kind.");
                input.kind = kind->second;
                if (source.Has("pointerType") && !source.Get("pointerType").IsUndefined())
                {
                    const auto type = source.Get("pointerType").As<Napi::String>().Utf8Value();
                    if (type == "touch") input.pointerType = BL_ARC_POINTER_TOUCH;
                    else if (type == "pen") input.pointerType = BL_ARC_POINTER_PEN;
                    else if (type != "mouse") throw Napi::TypeError::New(i.Env(), "Unknown pointer type.");
                }
                const auto number = [&](const char* name) {
                    return source.Has(name) && !source.Get(name).IsUndefined()
                        ? source.Get(name).As<Napi::Number>().DoubleValue() : 0.0;
                };
                input.clientX = number("clientX");
                input.clientY = number("clientY");
                input.deltaY = number("deltaY");
                const auto pointer = number("pointerId");
                const auto button = number("button");
                if (!std::isfinite(pointer) || std::trunc(pointer) != pointer ||
                    std::abs(pointer) > 9007199254740991.0 || !std::isfinite(button) ||
                    std::trunc(button) != button || button < 0 || button > UINT32_MAX)
                    throw Napi::TypeError::New(i.Env(), "Invalid pointer identity/button.");
                input.pointerId = static_cast<int64_t>(pointer);
                input.button = static_cast<uint32_t>(button);
                std::vector<bl_ArcRotateTouch> touches;
                if (source.Has("changedTouches"))
                {
                    const auto values = source.Get("changedTouches").As<Napi::Array>();
                    touches.reserve(values.Length());
                    for (uint32_t index = 0; index < values.Length(); ++index)
                    {
                        const auto touch = values.Get(index).As<Napi::Object>();
                        const double id = touch.Get("identifier").As<Napi::Number>().DoubleValue();
                        if (!std::isfinite(id) || std::trunc(id) != id || std::abs(id) > 9007199254740991.0)
                            throw Napi::TypeError::New(i.Env(), "Invalid touch identity.");
                        touches.push_back({static_cast<int64_t>(id),
                            touch.Get("clientX").As<Napi::Number>().DoubleValue(),
                            touch.Get("clientY").As<Napi::Number>().DoubleValue()});
                    }
                }
                input.changedTouches = touches.data();
                input.changedTouchCount = touches.size();
                bl_ArcRotateInputEffects effects{};
                const auto status = bl_processArcRotateInput(s.HandleOf<bl_ArcRotateControl>(i[0], Kind::Control), &input, &effects);
                SurfaceCallback(s, i.Env());
                s.Check(i.Env(), status);
                auto value = Napi::Object::New(i.Env());
                value.Set("preventDefault", effects.preventDefault);
                value.Set("capturePointer", effects.capturePointer);
                value.Set("releasePointer", effects.releasePointer);
                value.Set("pointerId", static_cast<double>(effects.pointerId));
                return value;
            });
            Bind(api, state, "detachControl", [](State& s, const Napi::CallbackInfo& i) {
                const auto control = s.HandleOf<bl_ArcRotateControl>(i[0], Kind::Control);
                s.Check(i.Env(), bl_detachControl(control));
                s.controlPredicates.erase(control._id);
                return i.Env().Undefined();
            });
            Bind(api, state, "createHemisphericLight", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_HemisphericLightOptions options{};
                if (!i[0].IsUndefined()) { options.hasDirection = true; options.direction = ColorValue(i[0]); }
                if (!i[1].IsUndefined()) options.intensity = {true, i[1].As<Napi::Number>().DoubleValue()};
                bl_HemisphericLight light{};
                s.Check(i.Env(), bl_createHemisphericLight(s.options.runtime, &options, &light));
                return s.Wrap(i.Env(), light, Kind::Light);
            });
            Bind(api, state, "getHemisphericLightProperties", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_HemisphericLightProperties p{};
                s.Check(i.Env(), bl_getHemisphericLightProperties(s.HandleOf<bl_HemisphericLight>(i[0], Kind::Light), &p));
                auto value = Napi::Object::New(i.Env());
                value.Set("direction", VectorObject(i.Env(), p.direction));
                value.Set("intensity", p.intensity);
                for (const auto& [name, member] : LightColors) value.Set(name, ColorArray(i.Env(), p.*member));
                return value;
            });
            Bind(api, state, "setHemisphericLightProperty", [](State& s, const Napi::CallbackInfo& i) {
                const auto light = s.HandleOf<bl_HemisphericLight>(i[0], Kind::Light);
                bl_HemisphericLightProperties p{};
                s.Check(i.Env(), bl_getHemisphericLightProperties(light, &p));
                const auto name = i[1].As<Napi::String>().Utf8Value();
                if (name == "direction") p.direction = Vector(i[2]);
                else if (name == "intensity") p.intensity = i[2].As<Napi::Number>().DoubleValue();
                else if (const auto member = LightColors.find(name); member != LightColors.end()) p.*member->second = ColorValue(i[2]);
                else throw Napi::TypeError::New(i.Env(), "Unsupported hemispheric property.");
                s.Check(i.Env(), bl_setHemisphericLightProperties(light, &p));
                return i.Env().Undefined();
            });
            Bind(api, state, "createDirectionalLight", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                const bl_DirectionalLightOptions options{ColorValue(i[0]),
                    i[1].IsUndefined() ? bl_OptionalNumber{} : bl_OptionalNumber{true, i[1].As<Napi::Number>().DoubleValue()}};
                bl_DirectionalLight light{};
                s.Check(i.Env(), bl_createDirectionalLight(s.options.runtime, &options, &light));
                return s.Wrap(i.Env(), light, Kind::DirectionalLight);
            });
            Bind(api, state, "getDirectionalLightProperties", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_DirectionalLight light{};
                s.Check(i.Env(), bl_lightAsDirectionalLight(s.LightOf(i[0]), &light));
                bl_DirectionalLightProperties p{};
                s.Check(i.Env(), bl_getDirectionalLightProperties(light, &p));
                auto value = Napi::Object::New(i.Env());
                value.Set("direction", VectorObject(i.Env(), p.direction));
                value.Set("intensity", p.intensity);
                for (const auto& [name, member] : DirectionalColors) value.Set(name, ColorArray(i.Env(), p.*member));
                return value;
            });
            Bind(api, state, "setDirectionalLightProperty", [](State& s, const Napi::CallbackInfo& i) {
                const auto light = s.HandleOf<bl_DirectionalLight>(i[0], Kind::DirectionalLight);
                bl_DirectionalLightProperties p{};
                s.Check(i.Env(), bl_getDirectionalLightProperties(light, &p));
                const auto name = i[1].As<Napi::String>().Utf8Value();
                const bl_Vec3 previousDirection = p.direction;
                if (name == "direction") p.direction = Vector(i[2]);
                else if (name == "intensity") p.intensity = i[2].As<Napi::Number>().DoubleValue();
                else if (const auto member = DirectionalColors.find(name); member != DirectionalColors.end()) p.*member->second = ColorValue(i[2]);
                else throw Napi::TypeError::New(i.Env(), "Unsupported directional property.");
                s.Check(i.Env(), bl_setDirectionalLightProperties(light, &p));
                if (name == "direction" && i[3].IsBoolean() && i[3].As<Napi::Boolean>().Value() &&
                    std::memcmp(&previousDirection, &p.direction, sizeof(p.direction)) == 0)
                {
                    s.Check(i.Env(), bl_markLightUboDirty(s.LightOf(i[0])));
                }
                return i.Env().Undefined();
            });
            for (const auto* operation : {"setLightIntensity", "markLightUboDirty"})
            {
                Bind(api, state, operation, [operation](State& s, const Napi::CallbackInfo& i) {
                    const auto light = s.LightOf(i[0]);
                    s.Check(i.Env(), std::string_view(operation) == "setLightIntensity"
                        ? bl_setLightIntensity(light, i[1].As<Napi::Number>().DoubleValue()) : bl_markLightUboDirty(light));
                    return i.Env().Undefined();
                });
            }
            Bind(api, state, "createStandardMaterial", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_StandardMaterial material{};
                s.Check(i.Env(), bl_createStandardMaterial(s.options.runtime, &material));
                return s.Wrap(i.Env(), material, Kind::StandardMaterial);
            });
            Bind(api, state, "getStandardMaterialProperties", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_StandardMaterialProperties p{};
                s.Check(i.Env(), bl_getStandardMaterialProperties(s.HandleOf<bl_StandardMaterial>(i[0], Kind::StandardMaterial), &p));
                auto value = Napi::Object::New(i.Env());
                for (const auto& [name, member] : StandardColors) value.Set(name, ColorArray(i.Env(), p.*member));
                value.Set("alpha", p.alpha);
                value.Set("specularPower", p.specularPower);
                value.Set("backFaceCulling", p.backFaceCulling);
                value.Set("disableLighting", p.disableLighting);
                return value;
            });
            Bind(api, state, "setStandardMaterialProperty", [](State& s, const Napi::CallbackInfo& i) {
                const auto material = s.HandleOf<bl_StandardMaterial>(i[0], Kind::StandardMaterial);
                bl_StandardMaterialProperties p{};
                s.Check(i.Env(), bl_getStandardMaterialProperties(material, &p));
                const auto name = i[1].As<Napi::String>().Utf8Value();
                if (const auto member = StandardColors.find(name); member != StandardColors.end()) p.*member->second = ColorValue(i[2]);
                else if (name == "alpha") p.alpha = i[2].As<Napi::Number>().DoubleValue();
                else if (name == "specularPower") p.specularPower = i[2].As<Napi::Number>().DoubleValue();
                else if (name == "backFaceCulling") p.backFaceCulling = i[2].As<Napi::Boolean>().Value();
                else if (name == "disableLighting") p.disableLighting = i[2].As<Napi::Boolean>().Value();
                else throw Napi::TypeError::New(i.Env(), "Unsupported Standard property.");
                s.Check(i.Env(), bl_setStandardMaterialProperties(material, &p));
                return i.Env().Undefined();
            });
            Bind(api, state, "markMaterialUboDirty", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(), bl_markMaterialUboDirty(s.MaterialFamily(i[0])));
                return i.Env().Undefined();
            });
            Bind(api, state, "rebuildMaterial", [](State& s, const Napi::CallbackInfo& i) {
                const auto source = Options(i[2]);
                const bl_RebuildMaterialOptions options{OptionalBool(source, "rebuildViews"), OptionalBool(source, "rebuildFrameGraph")};
                s.Check(i.Env(), bl_rebuildMaterial(s.HandleOf<bl_SceneContext>(i[0], Kind::Scene), s.MaterialFamily(i[1]), &options));
                return i.Env().Undefined();
            });
            Bind(api, state, "rebuildSceneRenderables", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(), bl_rebuildSceneRenderables(s.HandleOf<bl_SceneContext>(i[0], Kind::Scene)));
                return i.Env().Undefined();
            });
            Bind(api, state, "disposeStandardMaterial", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(), bl_disposeStandardMaterial(s.HandleOf<bl_StandardMaterial>(i[0], Kind::StandardMaterial)));
                return i.Env().Undefined();
            });
            Bind(api, state, "getMeshProperties2", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_MeshProperties2 p{};
                s.Check(i.Env(), bl_getMeshProperties2(s.HandleOf<bl_Mesh>(i[0], Kind::Mesh), &p));
                auto value = Napi::Object::New(i.Env());
                value.Set("material", s.WrapFamily(i.Env(), p.material));
                value.Set("renderOrder", p.renderOrder.present ? Napi::Value(Napi::Number::New(i.Env(), p.renderOrder.value)) : i.Env().Undefined());
                value.Set("receiveShadows", p.receiveShadows);
                return value;
            });
            Bind(api, state, "setMeshProperty2", [](State& s, const Napi::CallbackInfo& i) {
                const auto mesh = s.HandleOf<bl_Mesh>(i[0], Kind::Mesh);
                bl_MeshProperties2 p{};
                s.Check(i.Env(), bl_getMeshProperties2(mesh, &p));
                const auto name = i[1].As<Napi::String>().Utf8Value();
                if (name == "material") p.material = s.MaterialFamily(i[2]);
                else if (name == "renderOrder") p.renderOrder = i[2].IsUndefined() ? bl_OptionalNumber{} : bl_OptionalNumber{true, i[2].As<Napi::Number>().DoubleValue()};
                else if (name == "receiveShadows") p.receiveShadows = i[2].As<Napi::Boolean>().Value();
                else throw Napi::TypeError::New(i.Env(), "Unknown mesh property.");
                s.Check(i.Env(), bl_setMeshProperties2(mesh, &p));
                return i.Env().Undefined();
            });
            Bind(api, state, "getSceneProperties2", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_SceneProperties2 p{};
                s.Check(i.Env(), bl_getSceneProperties2(s.HandleOf<bl_SceneContext>(i[0], Kind::Scene), &p));
                auto value = Napi::Object::New(i.Env());
                auto color = Napi::Object::New(i.Env());
                color.Set("r", p.clearColor.r); color.Set("g", p.clearColor.g);
                color.Set("b", p.clearColor.b); color.Set("a", p.clearColor.a);
                value.Set("clearColor", color);
                value.Set("camera", s.WrapFamily(i.Env(), p.camera));
                value.Set("fixedDeltaMs", p.fixedDeltaMs);
                return value;
            });
            Bind(api, state, "setSceneProperty2", [](State& s, const Napi::CallbackInfo& i) {
                const auto scene = s.HandleOf<bl_SceneContext>(i[0], Kind::Scene);
                bl_SceneProperties2 p{};
                s.Check(i.Env(), bl_getSceneProperties2(scene, &p));
                const auto name = i[1].As<Napi::String>().Utf8Value();
                if (name == "camera") p.camera = s.CameraFamily(i[2]);
                else if (name == "fixedDeltaMs") p.fixedDeltaMs = i[2].As<Napi::Number>().DoubleValue();
                else if (name == "clearColor")
                {
                    const auto value = i[2].As<Napi::Object>();
                    p.clearColor = {value.Get("r").As<Napi::Number>().DoubleValue(), value.Get("g").As<Napi::Number>().DoubleValue(),
                        value.Get("b").As<Napi::Number>().DoubleValue(), value.Get("a").As<Napi::Number>().DoubleValue()};
                }
                else throw Napi::TypeError::New(i.Env(), "Unknown scene property.");
                s.Check(i.Env(), bl_setSceneProperties2(scene, &p));
                return i.Env().Undefined();
            });
            Bind(api, state, "cameraAsFreeCamera", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_FreeCamera camera{};
                s.Check(i.Env(), bl_cameraAsFreeCamera(s.CameraFamily(i[0]), &camera));
                return s.Wrap(i.Env(), camera, Kind::Camera);
            });
            Bind(api, state, "cameraAsArcRotateCamera", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_ArcRotateCamera camera{};
                s.Check(i.Env(), bl_cameraAsArcRotateCamera(s.CameraFamily(i[0]), &camera));
                return s.Wrap(i.Env(), camera, Kind::ArcCamera);
            });
            Bind(api, state, "materialAsShaderMaterial", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_ShaderMaterial material{};
                s.Check(i.Env(), bl_materialAsShaderMaterial(s.MaterialFamily(i[0]), &material));
                return s.Wrap(i.Env(), material, Kind::Material);
            });
            Bind(api, state, "materialAsStandardMaterial", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                bl_StandardMaterial material{};
                s.Check(i.Env(), bl_materialAsStandardMaterial(s.MaterialFamily(i[0]), &material));
                return s.Wrap(i.Env(), material, Kind::StandardMaterial);
            });
        }

        void BindExtras(Napi::Object api, const std::shared_ptr<State>& state)
        {
            Bind(api, state, "createSphere", [](State& s, const Napi::CallbackInfo& i) {
                const auto options = SphereOptions(i[1]);
                bl_Mesh mesh{};
                s.Check(i.Env(), bl_createSphere(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine),
                                                 &options, &mesh));
                return s.Wrap(i.Env(), mesh, Kind::Mesh);
            });
            Bind(api, state, "getNodeRotationQuaternion",
                 [](State& s, const Napi::CallbackInfo& i) {
                     bl_Quat value{};
                     s.Check(i.Env(), bl_getNodeRotationQuaternion(s.NodeOf(i[0]), &value));
                     auto result = Vector(i.Env(), {value.x, value.y, value.z});
                     result.Set("w", value.w);
                     return result;
                 });
            Bind(api, state, "setNodeRotationQuaternion",
                 [](State& s, const Napi::CallbackInfo& i) {
                     const auto source = i[1].As<Napi::Object>();
                     const auto xyz = Vector(source);
                     s.Check(i.Env(), bl_setNodeRotationQuaternion(
                                          s.NodeOf(i[0]),
                                          {xyz.x, xyz.y, xyz.z,
                                           source.Get("w").As<Napi::Number>().DoubleValue()}));
                     return i.Env().Undefined();
                 });
            Bind(api, state, "setShaderMatrix", [](State& s, const Napi::CallbackInfo& i) {
                const auto name = i[1].As<Napi::String>().Utf8Value();
                const auto values = Numbers(i[2]);
                if (values.size() != 16)
                {
                    throw Napi::RangeError::New(i.Env(), "A matrix requires exactly 16 numbers.");
                }
                bl_Mat4 matrix{};
                std::copy(values.begin(), values.end(), matrix.values);
                const auto material = s.HandleOf<bl_ShaderMaterial>(i[0], Kind::Material);
                s.Check(i.Env(), bl_setShaderMatrix(material, String(name), &matrix));
                s.RefreshUniform(material, name);
                return i.Env().Undefined();
            });
            for (const bool matrix : {false, true})
            {
                Bind(api, state, matrix ? "setShaderMatrixF32" : "setShaderUniformF32",
                     [matrix](State& s, const Napi::CallbackInfo& i) {
                         const auto name = i[1].As<Napi::String>().Utf8Value();
                         const auto material = s.HandleOf<bl_ShaderMaterial>(i[0], Kind::Material);
                         const auto values = F32(i[2]);
                         s.Check(i.Env(),
                                 matrix ? bl_setShaderMatrixF32(material, String(name), values)
                                        : bl_setShaderUniformF32(material, String(name), values));
                         s.RefreshUniform(material, name);
                         return i.Env().Undefined();
                     });
            }
            Bind(api, state, "getShaderTexture", [](State& s, const Napi::CallbackInfo& i) {
                const auto name = i[1].As<Napi::String>().Utf8Value();
                bl_Texture2D texture{};
                s.Check(i.Env(),
                        bl_getShaderTexture(s.HandleOf<bl_ShaderMaterial>(i[0], Kind::Material),
                                            String(name), &texture));
                return s.Wrap(i.Env(), texture, Kind::Texture);
            });
            Bind(api, state, "getTexture2DInfo", [](State& s, const Napi::CallbackInfo& i) {
                bl_Texture2DInfo info{};
                s.Check(i.Env(),
                        bl_getTexture2DInfo(s.HandleOf<bl_Texture2D>(i[0], Kind::Texture), &info));
                auto result = Napi::Object::New(i.Env());
                result.Set("width", info.width);
                result.Set("height", info.height);
                result.Set("srgb", info.srgb);
                return result;
            });
            Bind(api, state, "updateTexture2DFromPixels",
                 [](State& s, const Napi::CallbackInfo& i) {
                     const auto texture = s.HandleOf<bl_Texture2D>(i[1], Kind::Texture);
                     bl_Texture2DInfo info{};
                     s.Check(i.Env(), bl_getTexture2DInfo(texture, &info));
                     s.Check(i.Env(), bl_updateTexture2DFromPixels(
                                          s.HandleOf<bl_EngineContext>(i[0], Kind::Engine), texture,
                                          Bytes(i[2]), i[3].IsUndefined() ? 0 : UiUint(i[3]),
                                          i[4].IsUndefined() ? 0 : UiUint(i[4]),
                                          i[5].IsUndefined() ? info.width : UiUint(i[5]),
                                          i[6].IsUndefined() ? info.height : UiUint(i[6])));
                     return i.Env().Undefined();
                 });
            for (const bool resize : {false, true})
            {
                Bind(api, state, resize ? "resizeMeshGeometry" : "updateMeshGeometry",
                     [resize](State& s, const Napi::CallbackInfo& i) {
                         const auto geometry = MeshGeometry(i[2]);
                         const auto engine = s.HandleOf<bl_EngineContext>(i[0], Kind::Engine);
                         const auto mesh = s.HandleOf<bl_Mesh>(i[1], Kind::Mesh);
                         s.Check(i.Env(), resize ? bl_resizeMeshGeometry(engine, mesh, &geometry)
                                                 : bl_updateMeshGeometry(engine, mesh, &geometry));
                         return i.Env().Undefined();
                     });
            }
            Bind(api, state, "updateMeshGeometryCapacity",
                 [](State& s, const Napi::CallbackInfo& i) {
                     const auto geometry = MeshGeometry(i[2]);
                     const bl_OptionalNumber factor{
                         i[3].IsNumber(),
                         i[3].IsNumber() ? i[3].As<Napi::Number>().DoubleValue() : 0};
                     std::vector<bl_GeometryRange> vertices;
                     std::vector<bl_GeometryRange> indices;
                     bl_GeometryUpdateRanges ranges{};
                     if (!i[4].IsUndefined() && !i[4].IsNull())
                     {
                         const auto source = i[4].As<Napi::Object>();
                         const auto copy = [](Napi::Value value,
                                              std::vector<bl_GeometryRange>& result) {
                             const auto array = value.As<Napi::Array>();
                             if (array.Length() > 4096)
                             {
                                 throw Napi::RangeError::New(value.Env(),
                                                             "Geometry range budget exceeded.");
                             }
                             for (uint32_t index = 0; index < array.Length(); ++index)
                             {
                                 const auto range = array.Get(index).As<Napi::Object>();
                                 result.push_back(
                                     {UiUint(range.Get("offset")), UiUint(range.Get("count"))});
                             }
                         };
                         copy(source.Get("vertices"), vertices);
                         copy(source.Get("indices"), indices);
                         ranges = {vertices.data(), vertices.size(), indices.data(),
                                   indices.size()};
                     }
                     bl_GeometryCapacityResult capacity{};
                     s.Check(i.Env(), bl_updateMeshGeometryCapacity(
                                          s.HandleOf<bl_EngineContext>(i[0], Kind::Engine),
                                          s.HandleOf<bl_Mesh>(i[1], Kind::Mesh), &geometry, &factor,
                                          i[4].IsUndefined() || i[4].IsNull() ? nullptr : &ranges,
                                          &capacity));
                     auto result = Napi::Object::New(i.Env());
                     result.Set("stable", capacity.stable);
                     result.Set("vertexCapacity", static_cast<double>(capacity.vertexCapacity));
                     result.Set("indexCapacity", static_cast<double>(capacity.indexCapacity));
                     return result;
                 });
            using UpdateAttribute =
                bl_Status (*)(bl_EngineContext, bl_Mesh, bl_F32Span, size_t, const size_t*, size_t);
            const std::pair<const char*, UpdateAttribute> attributes[]{
                {"updateMeshPositions", bl_updateMeshPositions},
                {"updateMeshNormals", bl_updateMeshNormals},
                {"updateMeshColors", bl_updateMeshColors},
                {"updateMeshUvs", bl_updateMeshUvs},
                {"updateMeshUv2", bl_updateMeshUv2},
                {"updateMeshTangents", bl_updateMeshTangents}};
            for (const auto& [name, update] : attributes)
            {
                Bind(api, state, name, [update](State& s, const Napi::CallbackInfo& i) {
                    const size_t count = i[4].IsUndefined() ? 0 : UiUint(i[4]);
                    s.Check(i.Env(), update(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine),
                                            s.HandleOf<bl_Mesh>(i[1], Kind::Mesh), F32(i[2]),
                                            i[3].IsUndefined() ? 0 : UiUint(i[3]),
                                            i[4].IsUndefined() ? nullptr : &count,
                                            i[5].IsUndefined() ? 0 : UiUint(i[5])));
                    return i.Env().Undefined();
                });
            }
            using EngineOperation = bl_Status (*)(bl_EngineContext);
            const std::pair<const char*, EngineOperation> engines[]{
                {"stopEngine", bl_stopEngine},
                {"invalidateRenderBundles", bl_invalidateRenderBundles},
                {"waitForGpuIdle", bl_waitForGpuIdle},
                {"waitForGpuResourceRetirements", bl_waitForGpuResourceRetirements}};
            for (const auto& [name, operation] : engines)
            {
                Bind(api, state, name, [operation](State& s, const Napi::CallbackInfo& i) {
                    const auto status = operation(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine));
                    SettleStarts(s, i.Env());
                    SurfaceCallback(s, i.Env());
                    s.Check(i.Env(), status);
                    return i.Env().Undefined();
                });
            }
            Bind(api, state, "renderFrame", [](State& s, const Napi::CallbackInfo& i) {
                const auto status = bl_renderFrame(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine),
                                                   i[1].As<Napi::Number>().DoubleValue());
                SurfaceCallback(s, i.Env());
                s.Check(i.Env(), status);
                return i.Env().Undefined();
            });
            Bind(api, state, "unregisterScene", [](State& s, const Napi::CallbackInfo& i) {
                const auto scene = s.HandleOf<bl_SceneContext>(i[0], Kind::Scene);
                s.Check(i.Env(), bl_unregisterScene(scene));
                s.registeredScenes.erase(scene._id);
                return i.Env().Undefined();
            });
            Bind(api, state, "disposeScene", [](State& s, const Napi::CallbackInfo& i) {
                const auto scene = s.HandleOf<bl_SceneContext>(i[0], Kind::Scene);
                const auto status = bl_disposeScene(scene);
                if (status == BL_OK)
                {
                    s.registeredScenes.erase(scene._id);
                    std::erase_if(s.callbacks, [scene](const auto& value) {
                        return value->scene._id == scene._id;
                    });
                }
                SurfaceCallback(s, i.Env());
                s.Check(i.Env(), status);
                return i.Env().Undefined();
            });
            Bind(api, state, "onSceneDispose", [](State& s, const Napi::CallbackInfo& i) {
                auto callback = std::make_unique<BeforeCallback>();
                callback->owner = &s;
                callback->scene = s.HandleOf<bl_SceneContext>(i[0], Kind::Scene);
                callback->function = Napi::Persistent(i[1].As<Napi::Function>());
                callback->disposeCallback = true;
                auto* pointer = callback.get();
                s.callbacks.push_back(std::move(callback));
                const auto status =
                    bl_onSceneDispose(pointer->scene, SceneDisposed, pointer, &pointer->token);
                if (status != BL_OK)
                {
                    s.callbacks.pop_back();
                    s.Check(i.Env(), status);
                }
                return SceneCallbackIdentity(i.Env(), i[0], pointer->token);
            });
            Bind(api, state, "removeSceneCallback", [](State& s, const Napi::CallbackInfo& i) {
                const auto scene = s.HandleOf<bl_SceneContext>(i[0], Kind::Scene);
                const auto identity =
                    i[1].As<Napi::Object>().Get("_callback").As<Napi::String>().Utf8Value();
                const auto found = std::find_if(
                    s.callbacks.begin(), s.callbacks.end(), [scene, &identity](const auto& value) {
                        return value->scene._id == scene._id &&
                               std::to_string(value->token.value) == identity;
                    });
                if (found == s.callbacks.end())
                {
                    throw Napi::TypeError::New(i.Env(),
                                               "Unknown or retired native scene callback.");
                }
                s.Check(i.Env(), bl_removeSceneCallback(scene, (*found)->token));
                s.callbacks.erase(found);
                return i.Env().Undefined();
            });
            Bind(api, state, "getAudioEngineInfo", [](State& s, const Napi::CallbackInfo& i) {
                bl_AudioEngineInfo info{};
                s.Check(i.Env(), bl_getAudioEngineInfo(
                                     s.HandleOf<bl_AudioEngine>(i[0], Kind::AudioEngine), &info));
                auto result = Napi::Object::New(i.Env());
                result.Set("volume", info.volume);
                result.Set("currentTime", info.context.currentTime);
                result.Set("sampleRate", info.context.sampleRate);
                result.Set("state", static_cast<double>(info.context.state));
                result.Set("offline", info.context.offline);
                result.Set("audibleOutputAvailable", info.context.audibleOutputAvailable);
                return result;
            });
            Bind(api, state, "getMasterVolume", [](State& s, const Napi::CallbackInfo& i) {
                double volume{};
                s.Check(i.Env(), bl_getMasterVolume(
                                     s.HandleOf<bl_AudioEngine>(i[0], Kind::AudioEngine), &volume));
                return Napi::Number::New(i.Env(), volume);
            });
            Bind(api, state, "setMasterVolume", [](State& s, const Napi::CallbackInfo& i) {
                const auto options = Ramp(i[2]);
                s.Check(i.Env(),
                        bl_setMasterVolume(s.HandleOf<bl_AudioEngine>(i[0], Kind::AudioEngine),
                                           i[1].As<Napi::Number>().DoubleValue(), &options));
                return i.Env().Undefined();
            });
            Bind(api, state, "setSoundSourceVolume", [](State& s, const Napi::CallbackInfo& i) {
                const auto options = Ramp(i[2]);
                s.Check(i.Env(), bl_setSoundSourceVolume(
                                     s.HandleOf<bl_AudioInputSource>(i[0], Kind::SoundSource),
                                     i[1].As<Napi::Number>().DoubleValue(), &options));
                return i.Env().Undefined();
            });
            Bind(api, state, "audioUserGesture", [](State& s, const Napi::CallbackInfo& i) {
                s.Check(i.Env(),
                        bl_audioUserGesture(s.HandleOf<bl_AudioEngine>(i[0], Kind::AudioEngine)));
                return i.Env().Undefined();
            });
            Bind(api, state, "getHostServices", [](State& s, const Napi::CallbackInfo& i) {
                const bl_IOService* io{};
                s.Check(i.Env(), bl_getIOService(s.options.runtime, &io));
                auto result = Napi::Object::New(i.Env());
                result.Set("coreIO", io != nullptr);
                return result;
            });
        }
    }

    void Initialize(Napi::Env env, const HostOptions& options)
    {
        auto state = std::make_shared<State>();
        state->self = state;
        state->options = options;
        auto api = Napi::Object::New(env);
        api.Set("_owner", Napi::External<std::shared_ptr<State>>::New(env, new std::shared_ptr<State>(state),
                              [](Napi::Env, std::shared_ptr<State>* owner) { delete owner; }));
        BindUi(api, state);
        BindExtras(api, state);
        BindFamilies(api, state);
        Bind(api, state, "disposeEngine", [](State& s, const Napi::CallbackInfo& i) {
            const auto engine = s.HandleOf<bl_EngineContext>(i[0], Kind::Engine);
            s.Check(i.Env(), bl_disposeEngine(engine));
            SettleStarts(s, i.Env());
            std::erase_if(s.engines, [engine](const auto& value) { return value._id == engine._id; });
            s.engineObjects.erase(engine._id);
            SurfaceCallback(s, i.Env());
            return i.Env().Undefined();
        });
        Bind(api, state, "resizeEngine", [](State& s, const Napi::CallbackInfo& i) {
            auto target = s.options.nativeEngine.target;
            target.width = UiUint(i[1]);
            target.height = UiUint(i[2]);
            s.Check(i.Env(), bl_setNativeTarget(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine), &target));
            s.options.nativeEngine.target = target;
            return i.Env().Undefined();
        });

        Bind(api, state, "createEngine", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto source = Options(i[1]);
            const bl_EngineOptions options{OptionalBool(source, "useHighPrecisionMatrix")};
            auto native = s.options.nativeEngine;
            if (i[0].IsObject())
            {
                const auto canvas = i[0].As<Napi::Object>();
                if (canvas.Has("width"))
                    native.target.width = canvas.Get("width").As<Napi::Number>().Uint32Value();
                if (canvas.Has("height"))
                    native.target.height = canvas.Get("height").As<Napi::Number>().Uint32Value();
            }
            bl_EngineContext engine{};
            s.Check(i.Env(), bl_createEngine(s.options.runtime, &native, &options, &engine));
            s.options.nativeEngine.target = native.target;
            s.engines.push_back(engine);
            auto value = s.Wrap(i.Env(), engine, Kind::Engine).As<Napi::Object>();
            s.engineObjects.insert_or_assign(engine._id, Napi::Persistent(value));
            return value;
        });
        Bind(api, state, "createSceneContext", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_SceneContext scene{};
            s.Check(i.Env(), bl_createSceneContext(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine), &scene));
            return s.Wrap(i.Env(), scene, Kind::Scene);
        });
        Bind(api, state, "createFreeCamera", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_FreeCamera camera{};
            s.Check(i.Env(), bl_createFreeCamera(s.options.runtime, Vector(i[0]), Vector(i[1]), &camera));
            return s.Wrap(i.Env(), camera, Kind::Camera);
        });
        Bind(api, state, "createTransformNode", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const std::string name = i[0].IsUndefined() ? "" : i[0].As<Napi::String>().Utf8Value();
            bl_TransformNode node{};
            s.Check(i.Env(), bl_createTransformNode(s.options.runtime, String(name), nullptr, &node));
            return s.Wrap(i.Env(), node, Kind::Node);
        });
        Bind(api, state, "createBoxData", [](State& s, const auto& i) { return Geometry(s, i, false); });
        Bind(api, state, "createSphereData", [](State& s, const auto& i) { return Geometry(s, i, true); });
        Bind(api, state, "createFlatGroundData", [](State& s, const auto& i) { return Geometry(s, i, 2); });
        Bind(api, state, "createGround", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto options = GroundOptions(i[1]);
            bl_Mesh mesh{};
            s.Check(i.Env(), bl_createGround(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine), &options, &mesh));
            return s.Wrap(i.Env(), mesh, Kind::Mesh);
        });
        Bind(api, state, "createBox", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto options = BoxOptions(i[1]);
            bl_Mesh mesh{};
            s.Check(i.Env(), bl_createBox(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine), &options, &mesh));
            return s.Wrap(i.Env(), mesh, Kind::Mesh);
        });
        Bind(api, state, "createMeshFromData", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto name = i[1].As<Napi::String>().Utf8Value();
            const bl_MeshGeometry geometry{F32(i[2]), F32(i[3]), U32(i[4]), F32(i[5]), F32(i[6]), F32(i[7]), F32(i[8])};
            bl_Mesh mesh{};
            s.Check(i.Env(), bl_createMeshFromData(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine), String(name), &geometry, &mesh));
            return s.Wrap(i.Env(), mesh, Kind::Mesh);
        });
        Bind(api, state, "createShaderMaterial", Material);
        Bind(api, state, "setShaderFloat", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto name = i[1].As<Napi::String>().Utf8Value();
            const auto material = s.HandleOf<bl_ShaderMaterial>(i[0], Kind::Material);
            s.Check(i.Env(), bl_setShaderFloat(material, String(name),
                                 i[2].As<Napi::Number>().DoubleValue()));
            s.RefreshUniform(material, name);
            return i.Env().Undefined();
        });
        Bind(api, state, "setShaderVector3", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto name = i[1].As<Napi::String>().Utf8Value();
            const auto material = s.HandleOf<bl_ShaderMaterial>(i[0], Kind::Material);
            s.Check(i.Env(), bl_setShaderVector3(material, String(name), Vector(i[2])));
            s.RefreshUniform(material, name);
            return i.Env().Undefined();
        });
        Bind(api, state, "setShaderUniform", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto name = i[1].As<Napi::String>().Utf8Value();
            const auto values = Numbers(i[2]);
            const auto material = s.HandleOf<bl_ShaderMaterial>(i[0], Kind::Material);
            s.Check(i.Env(), bl_setShaderUniform(material, String(name), {values.data(), values.size()}));
            s.RefreshUniform(material, name);
            return i.Env().Undefined();
        });
        Bind(api, state, "getShaderUniform", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto name = i[1].As<Napi::String>().Utf8Value();
            const auto material = s.HandleOf<bl_ShaderMaterial>(i[0], Kind::Material);
            bl_ShaderUniformView values{};
            s.Check(i.Env(), bl_getShaderUniform(material, String(name), &values));
            if (values.values.count == 1)
            {
                return Napi::Number::New(i.Env(), values.values.data[0]);
            }
            const auto key = std::pair{material._id, name};
            const auto previous = s.uniformViews.find(key);
            if (previous != s.uniformViews.end() && !previous->second.Value().IsEmpty())
            {
                s.RefreshUniform(material, name);
                return previous->second.Value();
            }
            auto array = CopyArray<Napi::Float32Array>(i.Env(), values.values.data, values.values.count).As<Napi::Float32Array>();
            auto reference = Napi::Persistent(array);
            reference.Unref();
            s.uniformViews.insert_or_assign(key, std::move(reference));
            return array;
        });
        Bind(api, state, "setShaderTexture", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto name = i[1].As<Napi::String>().Utf8Value();
            const auto texture = i[2].IsNull() ? bl_Texture2D{} : s.HandleOf<bl_Texture2D>(i[2], Kind::Texture);
            s.Check(i.Env(), bl_setShaderTexture(s.HandleOf<bl_ShaderMaterial>(i[0], Kind::Material), String(name), texture));
            return i.Env().Undefined();
        });
        Bind(api, state, "createTexture2DFromPixels", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto source = Options(i[4]);
            const std::map<std::string, bl_AddressMode> addresses{
                {"clamp-to-edge", BL_ADDRESS_CLAMP_TO_EDGE},
                {"repeat", BL_ADDRESS_REPEAT},
                {"mirror-repeat", BL_ADDRESS_MIRROR_REPEAT},
            };
            const std::map<std::string, bl_FilterMode> filters{{"nearest", BL_FILTER_NEAREST}, {"linear", BL_FILTER_LINEAR}};
            bl_PixelsTexture2DOptions options{};
            if (source.Has("addressModeU"))
                options.addressModeU = addresses.at(source.Get("addressModeU").As<Napi::String>().Utf8Value());
            if (source.Has("addressModeV"))
                options.addressModeV = addresses.at(source.Get("addressModeV").As<Napi::String>().Utf8Value());
            if (source.Has("minFilter"))
                options.minFilter = filters.at(source.Get("minFilter").As<Napi::String>().Utf8Value());
            if (source.Has("magFilter"))
                options.magFilter = filters.at(source.Get("magFilter").As<Napi::String>().Utf8Value());
            if (source.Has("srgb"))
                options.srgb = source.Get("srgb").As<Napi::Boolean>().Value();
            bl_Texture2D texture{};
            s.Check(i.Env(), bl_createTexture2DFromPixels(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine), Bytes(i[1]),
                                 i[2].As<Napi::Number>().Uint32Value(), i[3].As<Napi::Number>().Uint32Value(), &options, &texture));
            return s.Wrap(i.Env(), texture, Kind::Texture);
        });
        for (const bool add : {false, true})
        {
            Bind(api, state, add ? "addToScene" : "removeFromScene", [add](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                const auto scene = s.HandleOf<bl_SceneContext>(i[0], Kind::Scene);
                s.Check(i.Env(), add ? bl_addToScene(scene, s.NodeOf(i[1])) : bl_removeFromScene(scene, s.NodeOf(i[1])));
                return i.Env().Undefined();
            });
        }
        Bind(api, state, "registerScene", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto scene = s.HandleOf<bl_SceneContext>(i[0], Kind::Scene);
            s.Check(i.Env(), bl_registerScene(scene));
            s.registeredScenes.insert_or_assign(scene._id, Napi::Persistent(i[0].As<Napi::Object>()));
            return i.Env().Undefined();
        });
        Bind(api, state, "onBeforeRender", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            auto callback = std::make_unique<BeforeCallback>();
            callback->owner = &s;
            callback->scene = s.HandleOf<bl_SceneContext>(i[0], Kind::Scene);
            callback->function = Napi::Persistent(i[1].As<Napi::Function>());
            auto* pointer = callback.get();
            s.callbacks.push_back(std::move(callback));
            const auto status = bl_onBeforeRender(pointer->scene, BeforeRender, pointer, &pointer->token);
            if (status != BL_OK)
            {
                s.callbacks.pop_back();
                s.Check(i.Env(), status);
            }
            return SceneCallbackIdentity(i.Env(), i[0], pointer->token);
        });
        Bind(api, state, "startEngine", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            auto callback = std::make_unique<StartCallback>(StartCallback{&s, Napi::Promise::Deferred::New(i.Env()), false});
            const auto promise = callback->deferred.Promise();
            const auto engine = s.HandleOf<bl_EngineContext>(i[0], Kind::Engine);
            auto* pointer = callback.get();
            s.starts.push_back(std::move(callback));
            const auto status = bl_startEngine(engine, FirstFrame, pointer);
            if (status != BL_OK)
            {
                try
                {
                    s.Check(i.Env(), status);
                }
                catch (const Napi::Error& error)
                {
                    if (error.Get("operation").IsUndefined())
                    {
                        error.Set("operation", "startEngine");
                    }
                    pointer->deferred.Reject(error.Value());
                }
                s.starts.pop_back();
            }
            return promise;
        });
        Bind(api, state, "setSubtreeVisible", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            s.Check(i.Env(), bl_setSubtreeVisible(s.NodeOf(i[0]), i[1].As<Napi::Boolean>().Value()));
            return i.Env().Undefined();
        });
        Bind(api, state, "getNodeVector", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto field = i[1].As<Napi::String>().Utf8Value();
            const auto node = s.NodeOf(i[0]);
            bl_Vec3 value{};
            if (field == "position")
                s.Check(i.Env(), bl_getNodePosition(node, &value));
            else if (field == "scaling")
                s.Check(i.Env(), bl_getNodeScaling(node, &value));
            else if (field == "rotation")
                s.Check(i.Env(), bl_getNodeRotation(node, &value));
            else if (field == "target")
                s.Check(i.Env(), bl_getCameraTarget(s.HandleOf<bl_FreeCamera>(i[0], Kind::Camera), &value));
            else
                throw std::invalid_argument("Unknown native vector property.");
            return Vector(i.Env(), value);
        });
        Bind(api, state, "setNodeVector", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto field = i[1].As<Napi::String>().Utf8Value();
            const auto value = Vector(i[2]);
            const auto node = s.NodeOf(i[0]);
            if (field == "position")
                s.Check(i.Env(), bl_setNodePosition(node, value));
            else if (field == "scaling")
                s.Check(i.Env(), bl_setNodeScaling(node, value));
            else if (field == "rotation")
                s.Check(i.Env(), bl_setNodeRotation(node, value));
            else if (field == "target")
                s.Check(i.Env(), bl_setCameraTarget(s.HandleOf<bl_FreeCamera>(i[0], Kind::Camera), value));
            else
                throw std::invalid_argument("Unknown native vector property.");
            return i.Env().Undefined();
        });
        Bind(api, state, "getNodeParent", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_SceneNode parent{};
            s.Check(i.Env(), bl_getNodeParent(s.NodeOf(i[0]), &parent));
            return s.WrapNode(i.Env(), parent);
        });
        Bind(api, state, "setNodeParent", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            s.Check(i.Env(), bl_setNodeParent(s.NodeOf(i[0]), s.NodeOf(i[1])));
            return i.Env().Undefined();
        });
        Bind(api, state, "getNodeChildren", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_NodeChildren children{};
            s.Check(i.Env(), bl_getNodeChildren(s.NodeOf(i[0]), &children));
            auto result = Napi::Array::New(i.Env(), children.count);
            for (uint32_t n = 0; n < children.count; ++n)
                result.Set(n, s.WrapNode(i.Env(), children.data[n]));
            return result;
        });
        for (const bool append : {false, true})
        {
            Bind(api, state, append ? "appendNodeChild" : "removeNodeChild", [append](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
                s.Check(i.Env(), append ? bl_appendNodeChild(s.NodeOf(i[0]), s.NodeOf(i[1])) : bl_removeNodeChild(s.NodeOf(i[0]), s.NodeOf(i[1])));
                return i.Env().Undefined();
            });
        }
        Bind(api, state, "getNodeVisible", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bool visible{};
            s.Check(i.Env(), bl_getNodeVisible(s.NodeOf(i[0]), &visible));
            return Napi::Boolean::New(i.Env(), visible);
        });
        Bind(api, state, "setNodeVisible", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            s.Check(i.Env(), bl_setNodeVisible(s.NodeOf(i[0]), i[1].As<Napi::Boolean>().Value()));
            return i.Env().Undefined();
        });
        Bind(api, state, "getNodeWorldMatrix", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_Mat4 matrix{};
            uint64_t version{};
            s.Check(i.Env(), bl_getNodeWorldMatrix(s.NodeOf(i[0]), &matrix, &version));
            return CopyArray<Napi::Float64Array>(i.Env(), matrix.values, 16);
        });
        Bind(api, state, "getNodeName", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_String name{};
            s.Check(i.Env(), bl_getNodeName(s.NodeOf(i[0]), &name));
            return Napi::String::New(i.Env(), name.data ? name.data : "", name.length);
        });
        Bind(api, state, "setNodeName", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto name = i[1].As<Napi::String>().Utf8Value();
            s.Check(i.Env(), bl_setNodeName(s.NodeOf(i[0]), String(name)));
            return i.Env().Undefined();
        });
        Bind(api, state, "getCameraProperties", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_CameraProperties properties{};
            s.Check(i.Env(), bl_getCameraProperties(s.HandleOf<bl_FreeCamera>(i[0], Kind::Camera), &properties));
            auto value = Napi::Object::New(i.Env());
            value.Set("fov", properties.fov);
            value.Set("nearPlane", properties.nearPlane);
            value.Set("farPlane", properties.farPlane);
            value.Set("speed", properties.speed);
            value.Set("angularSensitivity", properties.angularSensitivity);
            value.Set("inertia", properties.inertia);
            return value;
        });
        Bind(api, state, "setCameraProperty", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto camera = s.HandleOf<bl_FreeCamera>(i[0], Kind::Camera);
            bl_CameraProperties properties{};
            s.Check(i.Env(), bl_getCameraProperties(camera, &properties));
            const auto name = i[1].As<Napi::String>().Utf8Value();
            const double value = i[2].As<Napi::Number>().DoubleValue();
            if (name == "fov")
                properties.fov = value;
            else if (name == "nearPlane")
                properties.nearPlane = value;
            else if (name == "farPlane")
                properties.farPlane = value;
            else if (name == "speed")
                properties.speed = value;
            else if (name == "angularSensitivity")
                properties.angularSensitivity = value;
            else if (name == "inertia")
                properties.inertia = value;
            else
                throw std::invalid_argument("Unknown camera property.");
            s.Check(i.Env(), bl_setCameraProperties(camera, &properties));
            return i.Env().Undefined();
        });
        Bind(api, state, "getMeshProperties", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_MeshProperties properties{};
            s.Check(i.Env(), bl_getMeshProperties(s.HandleOf<bl_Mesh>(i[0], Kind::Mesh), &properties));
            auto value = Napi::Object::New(i.Env());
            value.Set("material", s.Wrap(i.Env(), properties.material, Kind::Material));
            value.Set("renderOrder", properties.renderOrder.present ? Napi::Value(Napi::Number::New(i.Env(), properties.renderOrder.value)) : i.Env().Undefined());
            value.Set("receiveShadows", properties.receiveShadows);
            return value;
        });
        Bind(api, state, "setMeshProperty", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto mesh = s.HandleOf<bl_Mesh>(i[0], Kind::Mesh);
            bl_MeshProperties properties{};
            s.Check(i.Env(), bl_getMeshProperties(mesh, &properties));
            const auto name = i[1].As<Napi::String>().Utf8Value();
            if (name == "material")
                properties.material = i[2].IsNull() ? bl_ShaderMaterial{} : s.HandleOf<bl_ShaderMaterial>(i[2], Kind::Material);
            else if (name == "renderOrder")
                properties.renderOrder = i[2].IsUndefined() ? bl_OptionalNumber{} : bl_OptionalNumber{true, i[2].As<Napi::Number>().DoubleValue()};
            else if (name == "receiveShadows")
                properties.receiveShadows = i[2].As<Napi::Boolean>().Value();
            else
                throw std::invalid_argument("Unknown mesh property.");
            s.Check(i.Env(), bl_setMeshProperties(mesh, &properties));
            return i.Env().Undefined();
        });
        Bind(api, state, "getSceneProperties", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_SceneProperties properties{};
            s.Check(i.Env(), bl_getSceneProperties(s.HandleOf<bl_SceneContext>(i[0], Kind::Scene), &properties));
            auto value = Napi::Object::New(i.Env());
            auto color = Napi::Object::New(i.Env());
            color.Set("r", properties.clearColor.r);
            color.Set("g", properties.clearColor.g);
            color.Set("b", properties.clearColor.b);
            color.Set("a", properties.clearColor.a);
            value.Set("clearColor", color);
            value.Set("camera", s.Wrap(i.Env(), properties.camera, Kind::Camera));
            value.Set("fixedDeltaMs", properties.fixedDeltaMs);
            return value;
        });
        Bind(api, state, "setSceneProperty", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto scene = s.HandleOf<bl_SceneContext>(i[0], Kind::Scene);
            bl_SceneProperties properties{};
            s.Check(i.Env(), bl_getSceneProperties(scene, &properties));
            const auto name = i[1].As<Napi::String>().Utf8Value();
            if (name == "camera")
                properties.camera = i[2].IsNull() ? bl_FreeCamera{} : s.HandleOf<bl_FreeCamera>(i[2], Kind::Camera);
            else if (name == "fixedDeltaMs")
                properties.fixedDeltaMs = i[2].As<Napi::Number>().DoubleValue();
            else if (name == "clearColor")
            {
                const auto value = i[2].As<Napi::Object>();
                properties.clearColor = {value.Get("r").As<Napi::Number>().DoubleValue(), value.Get("g").As<Napi::Number>().DoubleValue(),
                    value.Get("b").As<Napi::Number>().DoubleValue(), value.Get("a").As<Napi::Number>().DoubleValue()};
            }
            else
                throw std::invalid_argument("Unknown scene property.");
            s.Check(i.Env(), bl_setSceneProperties(scene, &properties));
            return i.Env().Undefined();
        });
        Bind(api, state, "getEngineStats", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            bl_EngineStats stats{};
            s.Check(i.Env(), bl_getEngineStats(s.HandleOf<bl_EngineContext>(i[0], Kind::Engine), &stats));
            auto value = Napi::Object::New(i.Env());
            value.Set("drawCallCount", static_cast<double>(stats.drawCallCount));
            value.Set("gpuFrameTimeMs", stats.gpuFrameTimeMs);
            return value;
        });
        Bind(api, state, "createAudioEngineAsync", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            auto source = Options(i[0]);
            bl_AudioEngineOptions options{};
            options.volume = Optional(source, "volume");
            options.parameterRampDuration = Optional(source, "parameterRampDuration");
            options.resumeOnPauseRetryInterval = Optional(source, "resumeOnPauseRetryInterval");
            options.resumeOnInteraction = OptionalBool(source, "resumeOnInteraction");
            options.resumeOnPause = OptionalBool(source, "resumeOnPause");
            if (source.Has("audioContext") && !source.Get("audioContext").IsNull())
            {
                throw std::invalid_argument("External browser AudioContexts cannot be passed to a native audio host.");
            }
            bl_AudioEngine engine{};
            s.Check(i.Env(), bl_createAudioEngineAsync(s.options.runtime, &options, &engine));
            bl_HostAudioContext context{};
            const bl_AudioService* service{};
            s.Check(i.Env(), bl_getAudioContext(engine, &context, &service));
            auto bridge = std::make_shared<AudioBinding>(context, *service);
            s.audioBindings.emplace(engine._id, bridge);
            auto result = s.Wrap(i.Env(), engine, Kind::AudioEngine).As<Napi::Object>();
            auto audioContext = bridge->Wrap(i.Env());
            audioContext.DefineProperties({Napi::PropertyDescriptor::Value("_engineOwner", result)});
            result.Set("audioContext", audioContext);
            return result;
        });
        Bind(api, state, "unlockAudioEngineAsync", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            s.Check(i.Env(), bl_unlockAudioEngineAsync(s.HandleOf<bl_AudioEngine>(i[0], Kind::AudioEngine)));
            return i.Env().Undefined();
        });
        Bind(api, state, "createSoundSourceAsync", [](State& s, const Napi::CallbackInfo& i) -> Napi::Value {
            const auto engine = s.HandleOf<bl_AudioEngine>(i[0], Kind::AudioEngine);
            const auto found = s.audioBindings.find(engine._id);
            if (found == s.audioBindings.end())
                throw Napi::Error::New(i.Env(), "Native audio context is unavailable.");
            const auto source = Options(i[2]);
            const auto name = source.Has("name") ? source.Get("name").As<Napi::String>().Utf8Value() : std::string{};
            bl_SoundSourceOptions options{};
            options.name = {name.data(), name.size()};
            options.volume = Optional(source, "volume");
            options.outBusAutoDefault = OptionalBool(source, "outBusAutoDefault");
            bl_AudioInputSource input{};
            s.Check(i.Env(), bl_createSoundSourceAsync(engine, found->second->Node(i[1]), &options, &input));
            auto result = s.Wrap(i.Env(), input, Kind::SoundSource).As<Napi::Object>();
            result.DefineProperties({Napi::PropertyDescriptor::Value("_inputOwner", i[1])});
            s.audioSources[engine._id].push_back(Napi::Persistent(result));
            return result;
        });
        env.Global().Set("_liteNative", api);
    }

    void Frame(Napi::Env env, double deltaMs)
    {
        const auto state = GetState(env);
        if (state->disposed)
        {
            state->Check(env, BL_DISPOSED);
        }
        state->frameTimings = {};
        state->DrainFinalized(env);
        const auto flush = env.Global().Get("_liteFlushReferenceValues");
        if (flush.IsFunction()) flush.As<Napi::Function>().Call({});
        const double monotonicTimeMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch())
                                           .count();
        for (const auto& [id, binding] : state->audioBindings)
        {
            state->Check(env, bl_pollAudioEngine({state->options.runtime, id}, monotonicTimeMs));
        }
        for (const auto engine : state->engines)
        {
            state->inNativeFrame = true;
            const auto begin = std::chrono::steady_clock::now();
            const auto status = bl_frame(engine, deltaMs);
            state->frameTimings.nativeFrameIncludingCallbacksMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            state->inNativeFrame = false;
            SettleStarts(*state, env);
            if (!state->callbackError.IsEmpty())
            {
                auto error = std::move(state->callbackError);
                throw error;
            }
            if (state->callbackCaptureFailed)
            {
                state->callbackCaptureFailed = false;
                throw Napi::Error::New(env, "Failed to retain a JavaScript callback exception.");
            }
            if (status != BL_STOPPED && status != BL_DISPOSED)
            {
                state->Check(env, status);
            }
            state->Prune();
            state->DrainFinalized(env);
        }
    }

    void Dispose(Napi::Env env)
    {
        const auto state = GetState(env);
        SettleStarts(*state, env);
        auto callbackError = std::move(state->callbackError);
        const bool captureFailed = state->callbackCaptureFailed;
        state->disposed = true;
        state->options.runtime = nullptr;
        state->objects.clear();
        state->nodeObjects.clear();
        state->engineObjects.clear();
        state->registeredScenes.clear();
        state->callbacks.clear();
        state->starts.clear();
        state->uiContexts.clear();
        state->uiCallbacks.clear();
        state->uniformViews.clear();
        for (auto& [id, binding] : state->audioBindings)
        {
            binding->Invalidate();
        }
        state->audioBindings.clear();
        state->audioSources.clear();
        state->callbackError = {};
        state->knownTokens.clear();
        state->knownUiListeners.clear();
        state->engines.clear();
        state->finalized.clear();
        state->familyKinds.clear();
        state->controlPredicates.clear();
        if (!callbackError.IsEmpty())
        {
            throw callbackError;
        }
        if (captureFailed)
        {
            throw Napi::Error::New(env, "Failed to retain a JavaScript callback exception during teardown.");
        }
    }

    FrameTimings GetFrameTimings(Napi::Env env)
    {
        return GetState(env)->frameTimings;
    }
}
