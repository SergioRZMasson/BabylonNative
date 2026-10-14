#include "Host.h"
#include "AssetIO.h"
#include "AssetInternal.h"
#include "PbrInternal.h"
#include <babylon_lite_asset_decoder.h>
#include <babylon_lite_shader_compiler.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <malloc.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include "GpuState.h"
#ifdef SCENE35_JAVASCRIPT
#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Plugins/LiteJSBinding.h>
#else
int Scene35UserMain();
#endif

namespace
{
    using Clock = std::chrono::steady_clock;
    double Ms(Clock::duration value) { return std::chrono::duration<double, std::milli>(value).count(); }
    uint64_t Cpu()
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            throw std::runtime_error("Process CPU query failed");
        ULARGE_INTEGER k{}, u{};
        k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
        return k.QuadPart + u.QuadPart;
    }
    struct Capture : bgfx::CallbackI
    {
        std::atomic<bool> done{};
        std::atomic<bool> failed{};
        void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* text) override
        {
            fprintf(stderr, "GPU fatal: %s\n", text);
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
                failed = true; done = true;
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
        bool ownsCom{};
        bool diagnosticDepth32{};
        bool diagnosticInvariant{};
        bl_ShaderCompilerService2 actualCompiler2{};
        Capture capture;
        bl_Runtime* runtime{};
        bl_AssetDecoder* decoder{};
        bl_NativeEngineOptions native{};
        Scene35Assets assets;
        bgfx::TextureHandle marker{BGFX_INVALID_HANDLE};
        bgfx::TextureHandle staging{BGFX_INVALID_HANDLE};
        uint8_t markerBytes[4]{};
        uint32_t waits{};
        uint32_t captureRequests{}, releaseRequests{}, preventRequests{}, actualCaptures{};
        std::atomic<int> pendingCapture{};
        uint32_t frames{121}, warmup{}, measure{};
        std::filesystem::path output;
        std::filesystem::path script;
        std::filesystem::path diagnosticShaderOracle;
        std::vector<Sample> samples;
        std::vector<double> deltas;
        std::string replay;
        double startup{};
        uint64_t measuredCpuBegin{}, measuredCpuEnd{};
        Clock::time_point created{Clock::now()};

        void Effects(bool capturePointer,bool releasePointer,bool preventDefault)
        {
            captureRequests+=capturePointer;releaseRequests+=releasePointer;
            preventRequests+=preventDefault;
            if(capturePointer) pendingCapture=1;
            if(releasePointer) pendingCapture=2;
        }
        void ApplyCapture()
        {
            const int effect=pendingCapture.exchange(0);
            if(effect==1)
            {
                SetCapture(window);
                if(GetCapture()!=window) throw std::runtime_error("Actual native pointer capture failed");
                ++actualCaptures;
            }
            else if(effect==2)
            {
                if(!ReleaseCapture()) throw std::runtime_error("Actual native pointer release failed");
            }
        }
        bl_Status Wait()
        {
            bgfx::TextureRegion from; from.init(marker);
            bgfx::TextureRegion to; to.init(staging);
            bgfx::blit(126, to, from);
            const uint32_t ready = bgfx::read(to, markerBytes);
            uint32_t frame = bgfx::frame();
            while (static_cast<int32_t>(frame - ready) < 0) frame = bgfx::frame();
            ++waits;
            return markerBytes[0] == 19 && markerBytes[1] == 37 ? BL_OK : BL_HOST_ERROR;
        }
        void Initialize()
        {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ownsCom = SUCCEEDED(com);
            if (FAILED(com) && com != RPC_E_CHANGED_MODE)
                throw std::runtime_error("COM initialization failed");
            bgfx::Init init;
            init.type = bgfx::RendererType::Direct3D11;
            init.callback = &capture;
            init.swapChain.nwh = window;
            init.swapChain.width = 1280; init.swapChain.height = 720;
            init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
            init.swapChain.formatDepthStencil = diagnosticDepth32 ? bgfx::TextureFormat::D32F
                                                                 : bgfx::TextureFormat::D24S8;
            init.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
            if (!bgfx::init(init)) throw std::runtime_error("Actual D3D11 initialization failed");
            initialized = true;
            const uint8_t pixel[4] = {19,37,53,255};
            marker = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(pixel,4));
            staging = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
            const bl_Allocator allocator{nullptr,
                [](void*,size_t bytes,size_t alignment){return _aligned_malloc(bytes,alignment);},
                [](void*,void* pointer,size_t,size_t){_aligned_free(pointer);}};
            bl_AssetService assetService{};
            Scene35Host::Check(bl_createAssetDecoder(&allocator,&decoder,&assetService));
            const auto compiler = bl_shaderCompilerService();
            auto compiler2 = bl_shaderCompilerService2();
            actualCompiler2=compiler2;
            if(diagnosticInvariant || !diagnosticShaderOracle.empty())
            {
                compiler2.userData=this;
                compiler2.compile=[](void* user,const bl_ShaderCompileRequest2* input,
                                     bl_ShaderCompileResult2* result)
                {
                    try
                    {
                        auto* host=static_cast<Host*>(user);
                        auto request=*input;
                        std::string vertex(input->base.vertex.wgsl.data,input->base.vertex.wgsl.length);
                        std::string fragment(input->base.fragment.wgsl.data,input->base.fragment.wgsl.length);
                        if(!host->diagnosticShaderOracle.empty() && input->instanceAttributeCount==4)
                        {
                            std::ifstream vs(host->diagnosticShaderOracle/"source-module-1.wgsl");
                            std::ifstream fs(host->diagnosticShaderOracle/"source-module-2.wgsl");
                            if(!vs || !fs) return BL_HOST_ERROR;
                            vertex.assign(std::istreambuf_iterator<char>(vs),{});
                            fragment.assign(std::istreambuf_iterator<char>(fs),{});
                        }
                        if(host->diagnosticInvariant)
                        {
                            const auto offset=vertex.find("@builtin(position)clipPos");
                            if(offset!=std::string::npos) vertex.insert(offset,"@invariant ");
                        }
                        request.base.vertex.wgsl={vertex.data(),vertex.size()};
                        request.base.fragment.wgsl={fragment.data(),fragment.size()};
                        return host->actualCompiler2.compile(host->actualCompiler2.userData,&request,result);
                    }
                    catch(const std::bad_alloc&){return BL_OUT_OF_MEMORY;}
                    catch(...){return BL_HOST_ERROR;}
                };
                compiler2.release=[](void* user,bl_ShaderCompileResult2* result)
                {
                    auto* host=static_cast<Host*>(user);
                    host->actualCompiler2.release(host->actualCompiler2.userData,result);
                };
            }
            const auto io = Scene35IO(&assets);
            const bl_RuntimeOptions2 options{sizeof(options),2,{allocator,nullptr,nullptr,
                &compiler,nullptr,&io},&assetService,&compiler2};
            Scene35Host::Check(bl_createRuntime2(&options,&runtime));
            native.ownership = BL_BGFX_BORROWED;
            native.backend = BL_RENDERER_D3D11;
            native.target = {BL_TARGET_SWAPCHAIN,BL_INVALID_BGFX_HANDLE,1280,720,
                BL_COLOR_BGRA8,diagnosticDepth32?BL_DEPTH_D32F:BL_DEPTH_D24S8,4,0,124};
            native.hostUserData = this;
            native.isExternalBgfxInitialized = [](void* user){return static_cast<Host*>(user)->initialized;};
            native.waitForSubmittedWork = [](void* user){return static_cast<Host*>(user)->Wait();};
        }
        void Observe()
        {
            if (measure) return;
            deltas.reserve(frames+1);
            for (size_t i=0;i<runtime->count;++i)
            {
                auto* record = runtime->records[i];
                if (!record || record->disposed || record->kind != L_SCENE) continue;
                bl_CallbackToken token{};
                Scene35Host::Check(bl_onBeforeRender({runtime,record->id},
                    [](void* user,double delta){static_cast<Host*>(user)->deltas.push_back(delta);},this,&token));
            }
        }
        bool Input(uint32_t frame, bl_ArcRotateInput& input, std::string& type)
        {
            if (replay.empty()) return false;
            input.pointerId=1; input.pointerType=BL_ARC_POINTER_MOUSE;
            if (replay=="wheel")
            {
                if (frame!=3) return false;
                input.kind=BL_ARC_WHEEL; input.deltaY=-100; type="wheel"; return true;
            }
            if (replay!="rotate" && replay!="pan") throw std::runtime_error("Unknown replay");
            if (frame<1 || frame>4) return false;
            input.clientX=frame<3?640:replay=="rotate"?740:665;
            input.clientY=frame<3?360:replay=="rotate"?400:350;
            input.button=replay=="rotate"?0:2;
            input.kind=frame==2?BL_ARC_POINTER_DOWN:frame==4?BL_ARC_POINTER_UP:BL_ARC_POINTER_MOVE;
            type=frame==2?"pointerdown":frame==4?"pointerup":"pointermove";
            return true;
        }
        void NativeInput(uint32_t frame)
        {
            bl_ArcRotateInput input{}; std::string type;
            if (!Input(frame,input,type)) return;
            for (size_t i=0;i<runtime->count;++i)
            {
                auto* record=runtime->records[i];
                if (!record || record->disposed || record->kind!=L_CONTROL) continue;
                bl_ArcRotateInputEffects effects{};
                Scene35Host::Check(bl_processArcRotateInput({runtime,record->id},&input,&effects));
                Effects(effects.capturePointer,effects.releasePointer,effects.preventDefault);
            }
            ApplyCapture();
        }
        void State(uint32_t frame)
        {
            std::ofstream file(output/("frame-"+std::to_string(frame)+".json"));
            file<<std::setprecision(17)<<"{\"frame\":"<<frame
                <<",\"width\":1280,\"height\":720,\"requestedMSAA\":4"
                <<",\"requestedFactorAnisotropy\":4,\"effectiveFactorAnisotropy\":1,\"meshes\":[";
            bool first=true; size_t controls=0;
            for(size_t i=0;i<runtime->count;++i)
            {
                auto* record=runtime->records[i];
                if(!record || record->disposed) continue;
                if(record->kind==L_CONTROL) ++controls;
                if(record->kind!=L_MESH) continue;
                auto* mesh=(L_Mesh*)record;
                if(!first) file<<','; first=false;
                file<<"{\"vertices\":"<<mesh->geometry.vertices<<",\"indices\":"<<mesh->geometry.indexCount
                    <<",\"instances\":"<<mesh->instanceCount<<",\"instanceVersion\":"<<mesh->instanceVersion
                    <<",\"gpuInstanceVersion\":"<<mesh->instanceGpuVersion<<"}";
            }
            file<<"],\"controls\":"<<controls;
            for(size_t i=0;i<runtime->count;++i)
            {
                auto* record=runtime->records[i];
                if(!record || record->disposed || record->kind!=L_SCENE) continue;
                auto* scene=(L_Scene*)record;
                for(size_t packet=0;packet<scene->pbrCapacity;++packet)
                {
                    const auto& pbr=scene->pbrPackets[packet];
                    if(!pbr.material) continue;
                    for(size_t uniform=0;uniform<pbr.pipeline.uniformCount;++uniform)
                    {
                        const auto& data=pbr.pipeline.uniforms[uniform];
                        if(data.byteCount!=368) continue;
                        file<<",\"submittedSceneUniforms\":[";
                        for(size_t component=0;component<92;++component)
                        {
                            float value{};memcpy(&value,data.bytes+component*4,4);
                            if(component) file<<',';file<<value;
                        }
                        file<<']';
                    }
                }
            }
            for(size_t i=0;i<runtime->count;++i)
            {
                auto* record=runtime->records[i];
                if(!record || record->disposed) continue;
                if(record->kind==L_ENGINE)
                {
                    bl_EngineStats stats{};
                    Scene35Host::Check(bl_getEngineStats({runtime,record->id},&stats));
                    file<<",\"drawCalls\":"<<stats.drawCallCount;
                }
                if(record->kind==L_ARC_CAMERA)
                {
                    bl_ArcRotateCameraProperties p{};
                    Scene35Host::Check(bl_getArcRotateCameraProperties({runtime,record->id},&p));
                    file<<",\"camera\":{\"alpha\":"<<p.alpha<<",\"beta\":"<<p.beta
                        <<",\"radius\":"<<p.radius<<",\"fov\":"<<p.fov
                        <<",\"nearPlane\":"<<p.nearPlane<<",\"farPlane\":"<<p.farPlane
                        <<",\"target\":["<<p.target.x<<','<<p.target.y<<','<<p.target.z<<"]}";
                }
            }
            file<<"}\n";
            if(!file.good()) throw std::runtime_error("State output failed");
        }
        void Step(uint32_t frame,const std::function<void()>& draw)
        {
            const auto begin=Clock::now(); const auto cpu=Cpu();
            draw(); const auto coreEnd=Clock::now();
            const bool captureFrame=!measure && (frame==1 || frame==12 || frame==61 || frame==121);
            if(captureFrame)
            {
                State(frame);
                const auto path=(output/("frame-"+std::to_string(frame)+".bgra8")).string();
                capture.done=false; bgfx::requestScreenShot(BGFX_INVALID_HANDLE,path.c_str());
            }
            const auto submitBegin=Clock::now(); bgfx::frame(); const auto end=Clock::now();
            const auto cpuEnd=Cpu();
            if(captureFrame)
            {
                while(!capture.done) bgfx::frame();
                if(capture.failed) throw std::runtime_error("Actual GPU capture failed");
                Scene35Host::Check(Wait());
                Scene35GpuState(output/("frame-"+std::to_string(frame)+"-actual-gpu-state.json"));
            }
            if(measure && frame>=warmup && frame<warmup+measure)
            {
                if(frame==warmup) measuredCpuBegin=cpu;
                measuredCpuEnd=cpuEnd;
                const auto* stats=bgfx::getStats();
                samples.push_back({Ms(end-begin),Ms(coreEnd-begin),Ms(end-submitBegin),
                    double(cpuEnd-cpu)/10000,stats->gpuFrameNum,
                    stats->gpuTimeBegin,stats->gpuTimeEnd,stats->gpuTimerFreq});
            }
        }
        void Receipt()
        {
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
            if(!GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof(memory)))
                throw std::runtime_error("Memory query failed");
            std::ofstream file(output/"run.json");
            file<<std::setprecision(17)<<"{\"exitCode\":0,\"startupMs\":"<<startup
                <<",\"backend\":\"bgfx/D3D11\",\"requestedMSAA\":4,\"effectiveMSAA\":null"
                <<",\"sourceInstances\":125,\"width\":1280,\"height\":720,\"fixedDeltaMs\":16.666666666666668"
                <<",\"warmup\":"<<warmup<<",\"measure\":"<<measure<<",\"replay\":\""<<replay
                <<"\",\"workingSetBytes\":"<<memory.WorkingSetSize<<",\"privateBytes\":"<<memory.PrivateUsage
                <<",\"measuredProcessCpuMs\":"<<double(measuredCpuEnd-measuredCpuBegin)/10000
                <<",\"requestedFactorAnisotropy\":4,\"effectiveFactorAnisotropy\":1"
                <<",\"diagnosticDepth32\":"<<(diagnosticDepth32?"true":"false")
                <<",\"diagnosticInvariant\":"<<(diagnosticInvariant?"true":"false")
                <<",\"captureRequests\":"<<captureRequests<<",\"releaseRequests\":"<<releaseRequests
                <<",\"preventDefaultRequests\":"<<preventRequests<<",\"actualWindowCaptures\":"<<actualCaptures
                <<",\"physicalInput\":false,\"measuredCaptureOrFileIO\":false,\"observedDeltas\":[";
            for(size_t i=0;i<deltas.size();++i){if(i) file<<','; file<<deltas[i];}
            file<<"],\"samples\":["; std::set<uint32_t> ids;
            for(size_t i=0;i<samples.size();++i)
            {
                const auto& s=samples[i]; if(i) file<<',';
                file<<"{\"completeHostWallMs\":"<<s.wall<<",\"coreAndCallbacksMs\":"<<s.core
                    <<",\"submitAndWaitMs\":"<<s.submit<<",\"processCpuMs\":"<<s.cpu<<",\"gpu\":";
                if(s.gpuFrequency>0 && s.gpuEnd>s.gpuBegin && ids.insert(s.gpuFrame).second)
                    file<<"{\"id\":"<<s.gpuFrame<<",\"begin\":\""<<s.gpuBegin<<"\",\"end\":\""
                        <<s.gpuEnd<<"\",\"frequency\":"<<s.gpuFrequency<<'}';
                else file<<"null";
                file<<'}';
            }
            file<<"]}\n";
            if(!file.good()) throw std::runtime_error("Run output failed");
        }
        void Finish()
        {
            if(runtime){Scene35Host::Check(bl_disposeRuntime(runtime));runtime=nullptr;}
            if(decoder){bl_destroyAssetDecoder(decoder);decoder=nullptr;}
            if(initialized)
            {
                bgfx::destroy(marker);bgfx::destroy(staging);bgfx::frame();bgfx::shutdown();
                initialized=false;
            }
            if(ownsCom){CoUninitialize();ownsCom=false;}
        }
    };
    Host* current;
}

namespace Scene35Host
{
    void Check(bl_Status status)
    {
        if(status==BL_OK) return;
        bl_Error error{};
        if(current && current->runtime) bl_getLastError(current->runtime,&error);
        throw std::runtime_error("C99 status "+std::to_string(status)+": "+
            (error.message.data?std::string(error.message.data,error.message.length):""));
    }
    bl_Runtime* Runtime(){return current->runtime;}
    bl_EngineContext CreateEngine()
    {
        bl_EngineContext engine{};
        Check(bl_createEngine(Runtime(),&current->native,nullptr,&engine));return engine;
    }
    std::string AssetPath(const std::string& name){return name;}
    void Run(bl_EngineContext engine)
    {
        current->startup=Ms(Clock::now()-current->created);current->Observe();
        for(uint32_t frame=0;frame<=current->frames;++frame)
        {
            current->NativeInput(frame);
            current->Step(frame,[&]{Check(bl_frame(engine,1000.0/60));});
        }
        Check(bl_stopEngine(engine));
    }
}

int main(int argc,char** argv)
{
    Host host; current=&host;
    try
    {
        for(int i=1;i<argc;++i)
        {
            const std::string arg=argv[i];
            if(arg.starts_with("--out=")) host.output=arg.substr(6);
            else if(arg.starts_with("--assets=")) host.assets.directory=arg.substr(9);
            else if(arg.starts_with("--frames=")) host.frames=static_cast<uint32_t>(std::stoul(arg.substr(9)));
            else if(arg.starts_with("--measure=")) host.measure=static_cast<uint32_t>(std::stoul(arg.substr(10)));
            else if(arg.starts_with("--warmup=")) host.warmup=static_cast<uint32_t>(std::stoul(arg.substr(9)));
            else if(arg.starts_with("--script=")) host.script=arg.substr(9);
            else if(arg.starts_with("--replay=")) host.replay=arg.substr(9);
            else if(arg=="--diagnostic-depth32") host.diagnosticDepth32=true;
            else if(arg=="--diagnostic-invariant") host.diagnosticInvariant=true;
            else if(arg.starts_with("--diagnostic-shader-oracle=")) host.diagnosticShaderOracle=arg.substr(27);
            else throw std::runtime_error("Unknown host option");
        }
        if(host.output.empty() || host.assets.directory.empty())
            throw std::runtime_error("--out and --assets required");
        std::filesystem::create_directories(host.output);
        if(host.measure) host.frames=host.warmup+host.measure-1;
        host.window=CreateWindowExW(0,L"STATIC",L"Scene35 original source projection",WS_POPUP,
            0,0,1280,720,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!host.window) throw std::runtime_error("Native window failed");
#ifdef SCENE35_JAVASCRIPT
        std::string error;
        Babylon::AppRuntime::Options vmOptions{};
        vmOptions.UnhandledExceptionHandler=[&](const Napi::Error& value){error=Napi::GetErrorString(value);};
        Babylon::AppRuntime vm(vmOptions);
        Babylon::ScriptLoader loader(vm);
        const auto dispatch=[&](const std::function<void(Napi::Env)>& action)
        {
            std::promise<void> done; auto future=done.get_future();
            vm.Dispatch([&](Napi::Env env)
            {
                try{action(env);done.set_value();}
                catch(...){done.set_exception(std::current_exception());}
            });
            future.get();
            host.ApplyCapture();
        };
        dispatch([&](Napi::Env env)
        {
            host.Initialize();
            Babylon::Polyfills::Console::Initialize(env,[&](const char* text,Babylon::Polyfills::Console::LogLevel level)
            {
                if(level==Babylon::Polyfills::Console::LogLevel::Error) error=text;
            });
            Babylon::Plugins::LiteJSBinding::Initialize(env,{host.runtime,host.native});
            auto performance=Napi::Object::New(env);
            performance.Set("now",Napi::Function::New(env,[](const Napi::CallbackInfo& info)
            {return Napi::Number::New(info.Env(),Ms(Clock::now().time_since_epoch()));}));
            env.Global().Set("performance",performance);
            env.RunScript(R"JS(
globalThis._scene35Events=new Map();
globalThis._scene35Effects=[];
globalThis._scene35Canvas={width:1280,height:720,dataset:{},
addEventListener(type,fn){_scene35Events.set(type,fn);},
removeEventListener(type){_scene35Events.delete(type);},
setPointerCapture(id){_scene35Effects.push(["capture",id]);},
releasePointerCapture(id){_scene35Effects.push(["release",id]);}};
globalThis.document={querySelector:()=>_scene35Canvas};
globalThis._scene35Input=event=>{
event.preventDefault=()=>_scene35Effects.push(["preventDefault",0]);
_scene35Events.get(event.type)?.(event);};
)JS");
        });
        std::ifstream stream(host.script.empty()?std::filesystem::path(SCENE35_BUNDLE_FILE):host.script);
        if(!stream) throw std::runtime_error("Original scene35 native bundle missing");
        const std::string script{std::istreambuf_iterator<char>(stream),{}};
        loader.Eval(script,"app:///scene35.ts");
        bool ready=false;
        for(unsigned i=0;!ready && i<600;++i)
        {
            dispatch([&](Napi::Env)
            {
                for(size_t j=0;j<host.runtime->count;++j)
                {
                    auto* record=host.runtime->records[j];
                    if(record && !record->disposed && record->kind==L_ENGINE &&
                        ((L_Engine*)record)->running) ready=true;
                }
            });
            if(!error.empty()) throw std::runtime_error(error);
            if(!ready) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(!ready) throw std::runtime_error("Original JS never completed source startup");
        host.startup=Ms(Clock::now()-host.created);
        dispatch([&](Napi::Env){host.Observe();});
        for(uint32_t frame=0;frame<=host.frames;++frame)
        {
            const auto begin=Clock::now();const auto cpu=Cpu();
            dispatch([&](Napi::Env env)
            {
                bl_ArcRotateInput input{};std::string type;
                if(host.Input(frame,input,type))
                {
                    std::ostringstream event;
                    event<<"_scene35Input({type:'"<<type<<"',pointerType:'mouse',pointerId:1,button:"
                        <<input.button<<",clientX:"<<input.clientX<<",clientY:"<<input.clientY
                        <<",deltaY:"<<input.deltaY<<"});";
                    env.RunScript(event.str());
                    const auto effects=env.Global().Get("_scene35Effects").As<Napi::Array>();
                    for(uint32_t i=0;i<effects.Length();++i)
                    {
                        const auto effect=effects.Get(i).As<Napi::Array>();
                        const auto kind=effect.Get(uint32_t(0)).As<Napi::String>().Utf8Value();
                        host.Effects(kind=="capture",kind=="release",kind=="preventDefault");
                    }
                    env.Global().Set("_scene35Effects",Napi::Array::New(env));
                }
                host.Step(frame,[&]{Babylon::Plugins::LiteJSBinding::Frame(env,1000.0/60);});
            });
            if(frame==0)
            {
                dispatch([&](Napi::Env env)
                {
                    const auto dataset=env.Global().Get("_scene35Canvas").As<Napi::Object>()
                        .Get("dataset").As<Napi::Object>();
                    if(!dataset.Get("ready").ToBoolean().Value())
                        throw std::runtime_error("Source continuation did not complete after first native frame");
                    host.startup=Ms(Clock::now()-host.created);
                    const auto stringify=env.Global().Get("JSON").As<Napi::Object>()
                        .Get("stringify").As<Napi::Function>();
                    const auto data=stringify.Call({dataset}).As<Napi::String>().Utf8Value();
                    std::ofstream file(host.output/"source-dataset.json");file<<data<<'\n';
                    if(!file.good()) throw std::runtime_error("Original source dataset receipt failed");
                });
            }
            if(host.measure && frame>=host.warmup)
            {
                host.samples.back().wall=Ms(Clock::now()-begin);
                host.samples.back().cpu=double(Cpu()-cpu)/10000;
                if(frame==host.warmup) host.measuredCpuBegin=cpu;
                host.measuredCpuEnd=Cpu();
            }
            if(!error.empty()) throw std::runtime_error(error);
        }
        dispatch([&](Napi::Env env)
        {
            host.Receipt();
            Babylon::Plugins::LiteJSBinding::Dispose(env);
            host.Finish();
        });
#else
        host.Initialize();
        if(Scene35UserMain()!=0) throw std::runtime_error("Canonical STANDARD user main failed");
        host.Receipt();host.Finish();
#endif
        DestroyWindow(host.window);return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
