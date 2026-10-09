#include "Host.h"
#include "LiteInternal.h"
#include "DirectionalLightInternal.h"
#include "HemisphericLightInternal.h"
#include "LightInternal.h"
#include <babylon_lite_shader_compiler.h>
#include <bgfx/bgfx.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <sstream>
#ifdef SCENE38_JAVASCRIPT
#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Plugins/LiteJSBinding.h>
#else
int Scene38UserMain();
#endif

namespace
{
    using Clock = std::chrono::steady_clock;
    double Ms(Clock::duration value) { return std::chrono::duration<double, std::milli>(value).count(); }
    uint64_t Cpu()
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            throw std::runtime_error("GetProcessTimes failed");
        ULARGE_INTEGER k{}, u{};
        k.LowPart = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;
        return k.QuadPart + u.QuadPart;
    }
    struct Capture : bgfx::CallbackI
    {
        std::atomic<bool> done{};
        std::atomic<bool> failed{};
        void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* text) override
        {
            std::fprintf(stderr, "GPU fatal: %s\n", text);
            std::_Exit(3);
        }
        void traceVargs(const char*, uint16_t, const char*, va_list) override {}
        void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
        void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
        void profilerEnd() override {}
        uint32_t cacheReadSize(uint64_t) override { return 0; }
        bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
        void cacheWrite(uint64_t, const void*, uint32_t) override {}
        void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
        void captureEnd() override {}
        void captureFrame(const void*, uint32_t) override {}
        void screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
            bgfx::TextureFormat::Enum format, const void* data, uint32_t, bool flip) override
        {
            if (!data || width != 1280 || height != 720 || pitch < width * 4 ||
                format != bgfx::TextureFormat::BGRA8)
            {
                failed = true;
                done = true;
                return;
            }
            std::ofstream file(path, std::ios::binary);
            const auto* bytes = static_cast<const char*>(data);
            for (uint32_t y = 0; y < height; ++y)
                file.write(bytes + (flip ? height - 1 - y : y) * pitch, width * 4);
            failed = !file.good();
            done = true;
        }
    };
    struct Sample
    {
        double wall{}, core{}, submit{}, cpu{};
        uint32_t gpuFrame{};
        int64_t gpuBegin{}, gpuEnd{}, gpuFrequency{};
    };
    struct Host
    {
        HWND window{};
        bool initialized{};
        Capture capture;
        bl_Runtime* runtime{};
        bl_NativeEngineOptions native{};
        bgfx::TextureHandle fence{BGFX_INVALID_HANDLE};
        uint8_t fenceBytes[4]{};
        uint32_t frames{121}, warmup{}, measure{};
        std::filesystem::path output;
        std::filesystem::path scriptOverride;
        std::vector<Sample> samples;
        std::vector<std::unique_ptr<std::function<void(double)>>> callbacks;
        std::exception_ptr callbackFailure;
        double startupMs{};
        std::string replay;
        std::vector<double> observedDeltas;
        uint32_t captureRequests{}, releaseRequests{}, preventRequests{}, actualCaptures{};
        std::atomic<int> pendingCapture{};
        Clock::time_point created{Clock::now()};
        uint64_t measuredCpuBegin{}, measuredCpuEnd{};
        void ApplyCapture()
        {
            const int effect = pendingCapture.exchange(0);
            if (effect == 1)
            {
                SetCapture(window);
                if (GetCapture() != window)
                    throw std::runtime_error("Actual window capture failed");
                ++actualCaptures;
            }
            else if (effect == 2)
            {
                if (!ReleaseCapture())
                    throw std::runtime_error("Actual window release failed");
            }
        }
        void Effects(bool capturePointer, bool releasePointer, bool preventDefault)
        {
            captureRequests += capturePointer;
            releaseRequests += releasePointer;
            preventRequests += preventDefault;
            if (capturePointer)
                pendingCapture = 1;
            if (releasePointer)
                pendingCapture = 2;
        }
        void Observe()
        {
            if (measure)
                return;
            observedDeltas.reserve(frames + 1);
            for (size_t i = 0; i < runtime->count; ++i)
            {
                const auto* record = runtime->records[i];
                if (!record || record->disposed || record->kind != L_SCENE)
                    continue;
                bl_CallbackToken token{};
                PrimitivesHost::Check(bl_onBeforeRender({runtime, record->id}, [](void* user, double delta) { static_cast<Host*>(user)->observedDeltas.push_back(delta); }, this, &token));
            }
        }
        bool Input(uint32_t frame, bl_ArcRotateInput& input, std::string& type)
        {
            if (replay.empty())
                return false;
            input.pointerId = 1;
            input.pointerType = BL_ARC_POINTER_MOUSE;
            if (replay == "wheel")
            {
                if (frame != 3)
                    return false;
                input.kind = BL_ARC_WHEEL;
                input.deltaY = -100;
                type = "wheel";
                return true;
            }
            if (replay != "rotate" && replay != "pan")
                throw std::runtime_error("Unknown input fixture");
            if (frame < 1 || frame > 4)
                return false;
            input.clientX = frame < 3 ? 640 : replay == "rotate" ? 740
                                                                 : 665;
            input.clientY = frame < 3 ? 360 : replay == "rotate" ? 400
                                                                 : 350;
            input.button = replay == "rotate" ? 0 : 2;
            input.kind = frame == 2 ? BL_ARC_POINTER_DOWN : frame == 4 ? BL_ARC_POINTER_UP
                                                                       : BL_ARC_POINTER_MOVE;
            type = frame == 2 ? "pointerdown" : frame == 4 ? "pointerup"
                                                           : "pointermove";
            return true;
        }
        void NativeInput(uint32_t frame)
        {
            bl_ArcRotateInput input{};
            std::string type;
            if (!Input(frame, input, type))
                return;
            size_t delivered{};
            for (size_t i = 0; i < runtime->count; ++i)
            {
                const auto* record = runtime->records[i];
                if (!record || record->disposed || record->kind != L_CONTROL)
                    continue;
                bl_ArcRotateInputEffects effects{};
                PrimitivesHost::Check(bl_processArcRotateInput({runtime, record->id}, &input, &effects));
                Effects(effects.capturePointer, effects.releasePointer, effects.preventDefault);
                ++delivered;
            }
            if (delivered)
                throw std::runtime_error("Original scene38 does not attach controls");
            ApplyCapture();
        }
        void Initialize()
        {
            bgfx::Init init;
            init.type = bgfx::RendererType::Direct3D11;
            init.callback = &capture;
            init.swapChain.nwh = window;
            init.swapChain.width = 1280;
            init.swapChain.height = 720;
            init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
            init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
            init.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
            if (!bgfx::init(init))
                throw std::runtime_error("Actual bgfx D3D11 init failed");
            initialized = true;
            uint8_t initial[4]{};
            fence = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_READ_BACK, bgfx::copy(initial, 4));
            if (!bgfx::isValid(fence))
                throw std::runtime_error("GPU fence texture failed");
            const auto compiler = bl_shaderCompilerService();
            bl_RuntimeOptions options{};
            options.allocator = {nullptr,
                [](void*, size_t bytes, size_t alignment) { return _aligned_malloc(bytes, alignment); },
                [](void*, void* pointer, size_t, size_t) { _aligned_free(pointer); }};
            options.shaderCompiler = &compiler;
            PrimitivesHost::Check(bl_createRuntime(&options, &runtime));
            native.ownership = BL_BGFX_BORROWED;
            native.backend = BL_RENDERER_D3D11;
            native.target = {BL_TARGET_SWAPCHAIN, BL_INVALID_BGFX_HANDLE, 1280, 720,
                BL_COLOR_BGRA8, BL_DEPTH_D24S8, 4, 0, 16};
            native.hostUserData = this;
            native.isExternalBgfxInitialized = [](void* user) { return static_cast<Host*>(user)->initialized; };
            native.waitForSubmittedWork = [](void* user) {
                auto& host = *static_cast<Host*>(user);
                bgfx::TextureRegion region{};
                region.handle = host.fence;
                const uint32_t ready = bgfx::read(region, host.fenceBytes);
                for (unsigned i = 0; i < 64; ++i)
                    if (bgfx::frame() >= ready)
                        return BL_OK;
                return BL_HOST_ERROR;
            };
        }
        void Finish()
        {
            if (runtime)
            {
                PrimitivesHost::Check(bl_disposeRuntime(runtime));
                runtime = nullptr;
                callbacks.clear();
            }
            if (initialized)
            {
                bgfx::destroy(fence);
                bgfx::frame();
                bgfx::shutdown();
                initialized = false;
            }
        }
        void State(uint32_t frame)
        {
            std::ofstream file(output / ("frame-" + std::to_string(frame) + ".json"));
            file << std::setprecision(17) << "{\"frame\":" << frame
                 << ",\"backend\":\"bgfx/D3D11\",\"requestedSampleCount\":4,"
                    "\"effectiveSampleCount\":null,\"width\":1280,\"height\":720";
            for (size_t i = 0; i < runtime->count; ++i)
            {
                const auto* record = runtime->records[i];
                if (!record || record->disposed)
                    continue;
                if (record->kind == L_ARC_CAMERA)
                {
                    bl_ArcRotateCameraProperties p{};
                    PrimitivesHost::Check(bl_getArcRotateCameraProperties({runtime, record->id}, &p));
                    bl_SceneNode node{};
                    PrimitivesHost::Check(bl_arcRotateCameraNode({runtime, record->id}, &node));
                    bl_Mat4 world{};
                    uint64_t version{};
                    PrimitivesHost::Check(bl_getNodeWorldMatrix(node, &world, &version));
                    file << ",\"camera\":{\"alpha\":" << p.alpha << ",\"beta\":" << p.beta
                         << ",\"radius\":" << p.radius << ",\"fov\":" << p.fov
                         << ",\"nearPlane\":" << p.nearPlane << ",\"farPlane\":" << p.farPlane
                         << ",\"target\":[" << p.target.x << ',' << p.target.y << ',' << p.target.z << ']'
                         << ",\"eye\":[" << world.values[12] << ',' << world.values[13]
                         << ',' << world.values[14] << "]}";
                }
                if (record->kind == L_ENGINE)
                {
                    bl_EngineStats stats{};
                    PrimitivesHost::Check(bl_getEngineStats({runtime, record->id}, &stats));
                    file << ",\"drawCallCount\":" << stats.drawCallCount;
                }
            }
            file << ",\"lights\":[";
            bool firstLight = true;
            for (size_t i = 0; i < runtime->count; ++i)
            {
                const auto* record = runtime->records[i];
                if (!record || record->disposed || record->kind != L_LIGHT)
                    continue;
                const auto* common = reinterpret_cast<const L_Light*>(record);
                if (!firstLight)
                    file << ',';
                firstLight = false;
                bl_Vec3 direction{};
                bl_Vec3 diffuse{};
                bl_Vec3 specular{};
                bl_Vec3 ground{};
                double intensity{};
                if (common->kind == L_LIGHT_DIRECTIONAL)
                {
                    bl_DirectionalLightProperties p{};
                    PrimitivesHost::Check(bl_getDirectionalLightProperties({runtime, record->id}, &p));
                    direction = p.direction;
                    diffuse = p.diffuse;
                    specular = p.specular;
                    intensity = p.intensity;
                }
                else if (common->kind == L_LIGHT_HEMISPHERIC)
                {
                    bl_HemisphericLightProperties p{};
                    PrimitivesHost::Check(bl_getHemisphericLightProperties({runtime, record->id}, &p));
                    direction = p.direction;
                    diffuse = p.diffuseColor;
                    specular = p.specularColor;
                    ground = p.groundColor;
                    intensity = p.intensity;
                }
                else
                    throw std::runtime_error("Unknown native light family");
                file << "{\"id\":" << record->id << ",\"kind\":\""
                     << (common->kind == L_LIGHT_DIRECTIONAL ? "directional" : "hemispheric")
                     << "\",\"direction\":[" << direction.x << ',' << direction.y << ',' << direction.z
                     << "],\"intensity\":" << intensity << ",\"diffuse\":[" << diffuse.x << ',' << diffuse.y << ',' << diffuse.z
                     << "],\"specular\":[" << specular.x << ',' << specular.y << ',' << specular.z
                     << "],\"ground\":[" << ground.x << ',' << ground.y << ',' << ground.z << "]}";
            }
            file << "],\"meshes\":[";
            bool first = true;
            for (size_t i = 0; i < runtime->count; ++i)
            {
                const auto* record = runtime->records[i];
                if (!record || record->disposed || record->kind != L_MESH)
                    continue;
                const auto* mesh = reinterpret_cast<const L_Mesh*>(record);
                bl_Vec3 position{};
                PrimitivesHost::Check(bl_getNodePosition({runtime, record->id}, &position));
                bl_String name{};
                PrimitivesHost::Check(bl_getNodeName({runtime, record->id}, &name));
                if (!first)
                    file << ',';
                first = false;
                bl_Mat4 world{};
                uint64_t worldVersion{};
                PrimitivesHost::Check(bl_getNodeWorldMatrix({runtime, record->id}, &world, &worldVersion));
                file << "{\"id\":" << record->id << ",\"name\":\"" << std::string(name.data, name.length)
                     << "\",\"materialId\":" << mesh->material._id
                     << ",\"position\":[" << position.x << ',' << position.y << ',' << position.z
                     << "],\"vertices\":" << mesh->geometry.vertices << ",\"indices\":" << mesh->geometry.indexCount
                     << ",\"boundsMin\":[" << mesh->geometry.minimum.x << ',' << mesh->geometry.minimum.y << ',' << mesh->geometry.minimum.z
                     << "],\"boundsMax\":[" << mesh->geometry.maximum.x << ',' << mesh->geometry.maximum.y << ',' << mesh->geometry.maximum.z
                     << "],\"worldMatrix\":[";
                for (size_t j = 0; j < 16; ++j)
                {
                    if (j)
                        file << ',';
                    file << world.values[j];
                }
                file << "],\"scaling\":[" << mesh->node.scale.x << ',' << mesh->node.scale.y << ',' << mesh->node.scale.z
                     << "],\"rotation\":[" << mesh->node.euler.x << ',' << mesh->node.euler.y << ',' << mesh->node.euler.z
                     << "],\"visible\":" << (mesh->node.visible ? "true" : "false") << "}";
                const char* streamNames[]{"positions", "normals", "uvs"};
                for (size_t stream = 0; stream < 3; ++stream)
                {
                    std::ofstream dataFile(output / ("frame-" + std::to_string(frame) + "-mesh-" + std::to_string(record->id) + "-" + streamNames[stream] + ".bin"), std::ios::binary);
                    const size_t components = stream == 2 ? 2 : 3;
                    dataFile.write(reinterpret_cast<const char*>(mesh->geometry.streams[stream]), static_cast<std::streamsize>(mesh->geometry.vertices * components * sizeof(float)));
                    if (!dataFile.good())
                        throw std::runtime_error("Geometry CPU stream receipt failed");
                }
                std::ofstream indexFile(output / ("frame-" + std::to_string(frame) + "-mesh-" + std::to_string(record->id) + "-indices.bin"), std::ios::binary);
                indexFile.write(reinterpret_cast<const char*>(mesh->geometry.indices), static_cast<std::streamsize>(mesh->geometry.indexCount * sizeof(uint32_t)));
                if (!indexFile.good())
                    throw std::runtime_error("Geometry index receipt failed");
            }
            file << "],\"materials\":[";
            first = true;
            for (size_t i = 0; i < runtime->count; ++i)
            {
                const auto* record = runtime->records[i];
                if (!record || record->disposed || record->kind != L_STANDARD)
                    continue;
                const auto& p = reinterpret_cast<const L_Material*>(record)->standardProperties;
                if (!first)
                    file << ',';
                first = false;
                file << "{\"id\":" << record->id << ",\"diffuseColor\":[" << p.diffuseColor.x << ','
                     << p.diffuseColor.y << ',' << p.diffuseColor.z << "],\"alpha\":" << p.alpha
                     << ",\"specularPower\":" << p.specularPower
                     << ",\"specularColor\":[" << p.specularColor.x << ',' << p.specularColor.y << ',' << p.specularColor.z
                     << "],\"emissiveColor\":[" << p.emissiveColor.x << ',' << p.emissiveColor.y << ',' << p.emissiveColor.z
                     << "],\"ambientColor\":[" << p.ambientColor.x << ',' << p.ambientColor.y << ',' << p.ambientColor.z
                     << "],\"backFaceCulling\":" << (p.backFaceCulling ? "true" : "false")
                     << ",\"disableLighting\":" << (p.disableLighting ? "true" : "false") << '}';
            }
            file << "],\"sceneMeshOrder\":[";
            bool firstPacket = true;
            for (size_t i = 0; i < runtime->count; ++i)
            {
                const auto* record = runtime->records[i];
                if (!record || record->disposed || record->kind != L_SCENE)
                    continue;
                const auto* scene = reinterpret_cast<const L_Scene*>(record);
                for (size_t j = 0; j < scene->memberCount; ++j)
                {
                    if (scene->members[j]->record.kind != L_MESH)
                        continue;
                    if (!firstPacket)
                        file << ',';
                    firstPacket = false;
                    file << scene->members[j]->record.id;
                }
                std::ofstream lightFile(output / ("frame-" + std::to_string(frame) + "-scene-lights.bin"), std::ios::binary);
                lightFile.write(reinterpret_cast<const char*>(scene->lightData), sizeof(scene->lightData));
                if (!lightFile.good())
                    throw std::runtime_error("Scene light packing CPU receipt failed");
            }
            file << "],\"geometryAndLightStreamsOrigin\":\"RetainedCoreCPUstate,notGPUbufferreadback\""
                 << ",\"engineUpdates\":" << observedDeltas.size();
            size_t sceneCount{};
            const L_Scene* submittedScene{};
            const L_Engine* submittedEngine{};
            for (size_t index = 0; index < runtime->count; ++index)
            {
                const auto* record = runtime->records[index];
                if (!record || record->disposed)
                    continue;
                if (record->kind == L_SCENE)
                {
                    submittedScene = reinterpret_cast<const L_Scene*>(record);
                    ++sceneCount;
                }
                if (record->kind == L_ENGINE)
                    submittedEngine = reinterpret_cast<const L_Engine*>(record);
            }
            if (sceneCount != 1 || !submittedEngine || !submittedScene)
            {
                throw std::runtime_error("Capture host requires the actual single-scene original workload");
            }
            const size_t submitted = static_cast<size_t>(submittedEngine->stats.drawCallCount);
            if (submitted > submittedScene->drawCapacity)
            {
                throw std::runtime_error("Invalid actual sorted packet count");
            }
            file << ",\"actualSortedCorePackets\":[";
            const auto* packets = static_cast<const L_Draw*>(submittedScene->drawScratch);
            for (size_t index = 0; index < submitted; ++index)
            {
                const auto& packet = packets[index];
                if (index)
                    file << ',';
                file << "{\"meshId\":" << packet.mesh->node.record.id << ",\"materialId\":" << packet.material->record.id
                     << ",\"order\":" << packet.order << ",\"sequence\":" << packet.sequence
                     << ",\"transparent\":" << (packet.transparent ? "true" : "false")
                     << ",\"vertices\":" << packet.mesh->geometry.vertices
                     << ",\"indices\":" << packet.mesh->geometry.indexCount << "}";
            }
            file << ']';
            file << "}\n";
            if (!file.good())
                throw std::runtime_error("State receipt write failed");
        }
        void Step(uint32_t frame, const std::function<void()>& draw)
        {
            const auto begin = Clock::now();
            const auto cpuBegin = Cpu();
            draw();
            const auto coreEnd = Clock::now();
            const bool captureFrame = !measure && (frame == 1 || frame == 12 || frame == 61 || frame == 121);
            std::string image;
            if (captureFrame)
            {
                State(frame);
                image = (output / ("frame-" + std::to_string(frame) + ".bgra")).string();
                capture.done = false;
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, image.c_str());
            }
            const auto submitBegin = Clock::now();
            bgfx::frame();
            const auto end = Clock::now();
            const auto cpuEnd = Cpu();
            if (captureFrame)
            {
                for (unsigned i = 0; !capture.done && i < 64; ++i)
                    bgfx::frame();
                if (!capture.done || capture.failed)
                    throw std::runtime_error("Actual GPU screenshot failed");
            }
            if (measure && frame >= warmup && frame < warmup + measure)
            {
                if (frame == warmup)
                    measuredCpuBegin = cpuBegin;
                measuredCpuEnd = cpuEnd;
                const auto* stats = bgfx::getStats();
                samples.push_back({Ms(end - begin), Ms(coreEnd - begin), Ms(end - submitBegin),
                    double(cpuEnd - cpuBegin) / 10000.0, stats->gpuFrameNum,
                    stats->gpuTimeBegin, stats->gpuTimeEnd, stats->gpuTimerFreq});
            }
        }
        void Receipt()
        {
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
                throw std::runtime_error("Process memory query failed");
            std::ofstream file(output / "run.json");
            file << std::setprecision(17)
                 << "{\"result\":0,\"backend\":\"bgfx/D3D11\",\"msaa\":4,\"effectiveChildMsaa\":null,\"width\":1280,\"height\":720,"
                 << "\"firstFrameDeltaZero\":true,\"fixedDeltaMs\":16.666666666666668,"
                 << "\"physicalInput\":false,\"measuredCaptures\":false,\"warmup\":" << warmup
                 << ",\"measure\":" << measure << ",\"startupMs\":" << startupMs
                 << ",\"workingSetBytes\":" << memory.WorkingSetSize << ",\"privateBytes\":" << memory.PrivateUsage
                 << ",\"processCpuTotalMeasuredMs\":" << double(measuredCpuEnd - measuredCpuBegin) / 10000
                 << ",\"replay\":\"" << replay << "\",\"captureRequests\":" << captureRequests
                 << ",\"releaseRequests\":" << releaseRequests << ",\"preventDefaultRequests\":" << preventRequests
                 << ",\"actualWindowCaptures\":" << actualCaptures << ",\"observedDeltas\":[";
            for (size_t i = 0; i < observedDeltas.size(); ++i)
            {
                if (i)
                    file << ',';
                file << observedDeltas[i];
            }
            file << ']'
                 << ",\"samples\":[";
            std::set<uint32_t> gpuIds;
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const auto& p = samples[i];
                if (i)
                    file << ',';
                file << "{\"completeHostWallMs\":" << p.wall << ",\"coreAndCallbacksMs\":" << p.core
                     << ",\"submitAndWaitMs\":" << p.submit << ",\"processCpuMs\":" << p.cpu << ",\"gpu\":";
                if (p.gpuFrequency > 0 && p.gpuEnd > p.gpuBegin && gpuIds.insert(p.gpuFrame).second)
                    file << "{\"id\":" << p.gpuFrame << ",\"begin\":\"" << p.gpuBegin << "\",\"end\":\""
                         << p.gpuEnd << "\",\"frequency\":" << p.gpuFrequency << '}';
                else
                    file << "null";
                file << '}';
            }
            file << "]}\n";
            if (!file.good())
                throw std::runtime_error("Run receipt write failed");
        }
    };
    Host* current;
    LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        return DefWindowProcW(window, message, w, l);
    }
    void Pump()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}
namespace Scene38Host
{
    void Check(bl_Status status)
    {
        if (status == BL_OK)
            return;
        bl_Error error{};
        if (current && current->runtime)
            bl_getLastError(current->runtime, &error);
        throw std::runtime_error("C99 status " + std::to_string(status) + ": " +
                                 (error.message.data ? std::string(error.message.data, error.message.length) : ""));
    }
    bl_Runtime* Runtime() { return current->runtime; }
    bl_EngineContext CreateEngine()
    {
        bl_EngineContext engine{};
        Check(bl_createEngine(Runtime(), &current->native, nullptr, &engine));
        return engine;
    }
    void RetainCallback(std::function<void(double)> callback, bl_SceneContext scene)
    {
        auto owned = std::make_unique<std::function<void(double)>>(std::move(callback));
        bl_CallbackToken token{};
        Check(bl_onBeforeRender(scene, [](void* user, double delta) noexcept {
            try
            {
                (*static_cast<std::function<void(double)>*>(user))(delta);
            }
            catch (...)
            {
                current->callbackFailure = std::current_exception();
            } }, owned.get(), &token));
        current->callbacks.push_back(std::move(owned));
    }
    void Run(bl_EngineContext engine)
    {
        current->startupMs = Ms(Clock::now() - current->created);
        current->Observe();
        for (uint32_t frame = 0; frame <= current->frames; ++frame)
        {
            current->NativeInput(frame);
            current->Step(frame, [&] {
                Check(bl_frame(engine, 1000.0 / 60));
                if (current->callbackFailure)
                    std::rethrow_exception(current->callbackFailure);
            });
        }
        Check(bl_stopEngine(engine));
    }
}
int main(int argc, char** argv)
{
    Host host;
    current = &host;
    const auto started = Clock::now();
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg.starts_with("--out="))
                host.output = arg.substr(6);
            else if (arg.starts_with("--frames="))
                host.frames = static_cast<uint32_t>(std::stoul(arg.substr(9)));
            else if (arg.starts_with("--measure="))
                host.measure = static_cast<uint32_t>(std::stoul(arg.substr(10)));
            else if (arg.starts_with("--warmup="))
                host.warmup = static_cast<uint32_t>(std::stoul(arg.substr(9)));
            else if (arg.starts_with("--script="))
                host.scriptOverride = arg.substr(9);
            else if (arg.starts_with("--replay="))
                host.replay = arg.substr(9);
            else
                throw std::runtime_error("Unknown host option");
        }
        if (host.output.empty())
            throw std::runtime_error("--out required");
        std::filesystem::create_directories(host.output);
        if (host.measure)
            host.frames = host.warmup + host.measure - 1;
        WNDCLASSW wc{};
        wc.lpfnWndProc = Procedure;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LiteScene38Host";
        RegisterClassW(&wc);
        RECT size{0, 0, 1280, 720};
        AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, FALSE);
        host.window = CreateWindowW(wc.lpszClassName, L"Scene38 original STANDARD projection", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, size.right - size.left, size.bottom - size.top,
            nullptr, nullptr, wc.hInstance, nullptr);
        if (!host.window)
            throw std::runtime_error("Native window failed");
#ifdef SCENE38_JAVASCRIPT
        std::string error;
        Babylon::AppRuntime::Options vmOptions{};
        vmOptions.UnhandledExceptionHandler = [&](const Napi::Error& value) { error = Napi::GetErrorString(value); };
        Babylon::AppRuntime vm(vmOptions);
        Babylon::ScriptLoader loader(vm);
        const auto dispatch = [&](const std::function<void(Napi::Env)>& action) {
            std::promise<void> done;
            auto future = done.get_future();
            vm.Dispatch([&](Napi::Env env) {
                try
                {
                    action(env);
                    done.set_value();
                }
                catch (...)
                {
                    done.set_exception(std::current_exception());
                }
            });
            const auto limit = Clock::now() + std::chrono::seconds(60);
            while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            {
                Pump();
                if (Clock::now() > limit)
                    throw std::runtime_error("VM dispatch timed out");
            }
            future.get();
            host.ApplyCapture();
        };
        dispatch([&](Napi::Env env) {
            host.Initialize();
            Babylon::Polyfills::Console::Initialize(env, [&](const char* text, Babylon::Polyfills::Console::LogLevel level) {
                if (level == Babylon::Polyfills::Console::LogLevel::Error)
                    error = text;
            });
            Babylon::Plugins::LiteJSBinding::Initialize(env, {host.runtime, host.native});
            auto performance = Napi::Object::New(env);
            performance.Set("now", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                return Napi::Number::New(info.Env(), Ms(Clock::now().time_since_epoch()));
            }));
            env.Global().Set("performance", performance);
            env.RunScript(R"JS(
                globalThis._primitivesEvents = new Map();
                globalThis._primitivesEffects = [];
                globalThis._primitivesCanvas = {
                    width:1280, height:720, dataset:{},
                    addEventListener(type, fn) { _primitivesEvents.set(type, fn); },
                    removeEventListener(type) { _primitivesEvents.delete(type); },
                    setPointerCapture(id) { _primitivesEffects.push(["capture",id]); },
                    releasePointerCapture(id) { _primitivesEffects.push(["release",id]); }
                };
                globalThis._primitivesInput = event => {
                    event.preventDefault = () => _primitivesEffects.push(["preventDefault",0]);
                    _primitivesEvents.get(event.type)?.(event);
                };
                globalThis.document={getElementById:()=>_primitivesCanvas};
            )JS");
        });
        std::ifstream stream(host.scriptOverride.empty()
                                 ? std::filesystem::path(SCENE38_BUNDLE_FILE)
                                 : host.scriptOverride);
        if (!stream)
            throw std::runtime_error("Original scene38 native bundle missing");
        const std::string script{std::istreambuf_iterator<char>(stream), {}};
        loader.Eval(script, "app:///scene38.ts");
        bool ready = false;
        for (unsigned i = 0; !ready && i < 600; ++i)
        {
            dispatch([&](Napi::Env) {
                for (size_t j = 0; j < host.runtime->count; ++j)
                {
                    auto* p = host.runtime->records[j];
                    if (p && !p->disposed && p->kind == L_ENGINE && reinterpret_cast<L_Engine*>(p)->running)
                        ready = true;
                }
            });
            if (!error.empty())
                throw std::runtime_error(error);
            if (!ready)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!ready)
            throw std::runtime_error("Original JS never started engine");
        host.startupMs = Ms(Clock::now() - started);
        dispatch([&](Napi::Env env) {
            const auto result = env.Global().Get("_scene38ApiTestResult");
            if (!result.IsUndefined())
            {
                const auto json = env.Global().Get("JSON").As<Napi::Object>().Get("stringify").As<Napi::Function>();
                const auto text = json.Call({result}).As<Napi::String>().Utf8Value();
                std::ofstream file(host.output / "api-tests.json");
                file << text << '\n';
                if (!file.good())
                    throw std::runtime_error("API test result receipt failed");
            }
            host.Observe();
        });
        for (uint32_t frame = 0; frame <= host.frames; ++frame)
        {
            const auto wall = Clock::now();
            const auto cpuBegin = Cpu();
            dispatch([&](Napi::Env env) {
                bl_ArcRotateInput input{};
                std::string type;
                if (host.Input(frame, input, type))
                {
                    std::ostringstream event;
                    event << "_primitivesInput({type:'" << type << "',pointerType:'mouse',pointerId:1,button:"
                          << input.button << ",clientX:" << input.clientX << ",clientY:" << input.clientY
                          << ",deltaY:" << input.deltaY << "});";
                    env.RunScript(event.str());
                    auto effects = env.Global().Get("_primitivesEffects").As<Napi::Array>();
                    for (uint32_t i = 0; i < effects.Length(); ++i)
                    {
                        auto effect = effects.Get(i).As<Napi::Array>();
                        const auto kind = effect.Get(uint32_t(0)).As<Napi::String>().Utf8Value();
                        host.Effects(kind == "capture", kind == "release", kind == "preventDefault");
                    }
                    env.Global().Set("_primitivesEffects", Napi::Array::New(env));
                }
                host.Step(frame, [&] { Babylon::Plugins::LiteJSBinding::Frame(env, 1000.0 / 60); });
            });
            if (host.measure && frame >= host.warmup)
            {
                host.samples.back().wall = Ms(Clock::now() - wall);
                host.samples.back().cpu = double(Cpu() - cpuBegin) / 10000;
                if (frame == host.warmup)
                    host.measuredCpuBegin = cpuBegin;
                host.measuredCpuEnd = Cpu();
            }
            if (!error.empty())
                throw std::runtime_error(error);
        }
        dispatch([&](Napi::Env env) {
            host.Receipt();
            host.Finish();
            Babylon::Plugins::LiteJSBinding::Dispose(env);
        });
#else
        host.Initialize();
        host.startupMs = Ms(Clock::now() - started);
        if (Scene38UserMain() != 0)
            throw std::runtime_error("Canonical STANDARD user main failed");
        host.Receipt();
        host.Finish();
#endif
        DestroyWindow(host.window);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
