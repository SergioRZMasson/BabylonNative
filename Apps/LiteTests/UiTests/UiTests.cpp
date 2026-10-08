#include "babylon_lite.h"
#include "babylon_lite_shader_compiler.h"
#include "UiInternal.h"
#include <bgfx/bgfx.h>
#include <bgfx/defines.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <thread>
#include <vector>
#include <atomic>

static unsigned failures;

class CaptureCallback final : public bgfx::CallbackI
{
public:
    std::atomic<unsigned> screenshots{0};
    std::atomic<unsigned> errors{0};

    void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* message) override
    {
        std::fprintf(stderr, "bgfx fatal: %s\n", message);
        std::abort();
    }

    void traceVargs(const char*, uint16_t, const char*, va_list) override
    {
    }

    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override
    {
    }

    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override
    {
    }

    void profilerEnd() override
    {
    }

    uint32_t cacheReadSize(uint64_t) override
    {
        return 0;
    }

    bool cacheRead(uint64_t, void*, uint32_t) override
    {
        return false;
    }

    void cacheWrite(uint64_t, const void*, uint32_t) override
    {
    }

    void screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
                     bgfx::TextureFormat::Enum format, const void* data, uint32_t size,
                     bool flip) override
    {
        if ((format != bgfx::TextureFormat::RGBA8 && format != bgfx::TextureFormat::BGRA8) ||
            pitch < width * 4 || (size_t)pitch * height > size)
        {
            ++errors;
            return;
        }
        FILE* file = nullptr;
        if (fopen_s(&file, path, "wb") || !file)
        {
            ++errors;
            return;
        }
        std::fprintf(file, "P6\n%u %u\n255\n", width, height);
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t* row = (const uint8_t*)data + (flip ? height - y - 1 : y) * pitch;
            for (uint32_t x = 0; x < width; ++x)
            {
                unsigned first = format == bgfx::TextureFormat::BGRA8 ? 2 : 0;
                unsigned third = format == bgfx::TextureFormat::BGRA8 ? 0 : 2;
                const uint8_t pixel[] = {row[x * 4 + first], row[x * 4 + 1], row[x * 4 + third]};
                std::fwrite(pixel, 1, 3, file);
            }
        }
        std::fclose(file);
        ++screenshots;
    }

    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override
    {
        ++errors;
    }

    void captureEnd() override
    {
    }

    void captureFrame(const void*, uint32_t) override
    {
        ++errors;
    }
};

static void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++failures;
    }
}

static size_t PrivateBytes()
{
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&counters,
                              sizeof(counters)))
    {
        return 0;
    }
    return counters.PrivateUsage;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

static bl_String String(const char* value)
{
    return {value, std::strlen(value)};
}

struct Memory
{
    size_t live;
    size_t peak;
    size_t calls;
    size_t failAt;
};

struct Compiler
{
    bl_ShaderCompilerService inner;
    size_t compiles;
    size_t releases;
    bool invalidReflection;
    bool fail;
};

static bl_Status Compile(void* user, const bl_ShaderCompileRequest* request,
                          bl_ShaderCompileResult* result)
{
    Compiler* compiler = (Compiler*)user;
    ++compiler->compiles;
    bl_Status status = compiler->inner.compile(compiler->inner.userData, request, result);
    if (compiler->invalidReflection)
    {
        result->attributeCount = SIZE_MAX;
    }
    return compiler->fail ? BL_SHADER_ERROR : status;
}

static void Release(void* user, bl_ShaderCompileResult* result)
{
    Compiler* compiler = (Compiler*)user;
    ++compiler->releases;
    compiler->inner.release(compiler->inner.userData, result);
}

static void* Allocate(void* user, size_t bytes, size_t alignment)
{
    Memory* memory = (Memory*)user;
    ++memory->calls;
    if (memory->failAt && memory->calls >= memory->failAt)
    {
        return nullptr;
    }
    void* pointer = _aligned_malloc(bytes, alignment);
    if (pointer)
    {
        memory->live += bytes;
        if (memory->live > memory->peak)
        {
            memory->peak = memory->live;
        }
    }
    return pointer;
}

static void Deallocate(void* user, void* pointer, size_t bytes, size_t)
{
    Memory* memory = (Memory*)user;
    memory->live -= bytes;
    _aligned_free(pointer);
}

struct FaultIo
{
    bl_Runtime* runtime;
    unsigned reads;
    unsigned decoded;
    unsigned buffersReleased;
    unsigned imagesReleased;
    bool failRead;
    bool failDecode;
    bool invalidStride;
    bl_Status reentryStatus;
    uint8_t encoded[4];
    uint8_t pixels[16];
};

static bl_Status ReadImageFile(void* user, bl_String, bl_HostBuffer* result)
{
    FaultIo* io = (FaultIo*)user;
    ++io->reads;
    *result = {io->encoded, sizeof(io->encoded), io};
    bl_Error error = {};
    io->reentryStatus = bl_getLastError(io->runtime, &error);
    return io->failRead ? BL_HOST_ERROR : BL_OK;
}

static bl_Status DecodeImage(void* user, bl_Bytes encoded, bl_HostImage* result)
{
    FaultIo* io = (FaultIo*)user;
    ++io->decoded;
    CHECK(encoded.data == io->encoded);
    CHECK(encoded.count == sizeof(io->encoded));
    *result = {io->pixels, 2, 2, io->invalidStride ? 3u : 8u, io};
    return io->failDecode ? BL_HOST_ERROR : BL_OK;
}

static void ReleaseImageBuffer(void* user, bl_HostBuffer* result)
{
    FaultIo* io = (FaultIo*)user;
    ++io->buffersReleased;
    CHECK(result->allocation == io);
    *result = {};
}

static void ReleaseDecodedImage(void* user, bl_HostImage* result)
{
    FaultIo* io = (FaultIo*)user;
    ++io->imagesReleased;
    CHECK(result->allocation == io);
    *result = {};
}

static void Error(void*, const bl_Error* error)
{
    if (error->status == BL_DISPOSED || error->status == BL_OUT_OF_MEMORY)
    {
        return;
    }
    std::fprintf(stderr, "Lite error %u %.*s: %.*s\n", (unsigned)error->status,
                 (int)error->operation.length, error->operation.data,
                 (int)error->message.length, error->message.data);
}

struct Host
{
    HWND window;
    bool initialized;
    bgfx::TextureHandle color;
    bgfx::TextureHandle depth;
    bgfx::TextureHandle staging;
    bgfx::FrameBufferHandle target;
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> pixels;
    unsigned waits;
    bool failWait;
    uint16_t readbackView;
};

static bool Initialized(void* user)
{
    return ((Host*)user)->initialized;
}

static bl_Status Readback(void* user)
{
    Host* host = (Host*)user;
    ++host->waits;
    if (host->failWait)
    {
        return BL_HOST_ERROR;
    }
    bgfx::TextureRegion source;
    source.init(host->color);
    bgfx::TextureRegion destination;
    destination.init(host->staging);
    bgfx::blit(host->readbackView ? host->readbackView : 240, destination, source);
    host->pixels.resize((size_t)host->width * host->height * 4);
    uint32_t ready = bgfx::read(destination, host->pixels.data());
    for (unsigned i = 0; i < 32; ++i)
    {
        if ((int32_t)(bgfx::frame() - ready) >= 0)
        {
            return BL_OK;
        }
    }
    return BL_HOST_ERROR;
}

static void Capture(const Host& host, const char* path)
{
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") || !file)
    {
        CHECK(false);
        return;
    }
    std::fprintf(file, "P6\n%u %u\n255\n", host.width, host.height);
    for (size_t i = 0; i < host.pixels.size(); i += 4)
    {
        std::fwrite(host.pixels.data() + i, 1, 3, file);
    }
    std::fclose(file);
}

static const uint8_t* Pixel(const Host& host, uint32_t x, uint32_t y)
{
    return host.pixels.data() + ((size_t)y * host.width + x) * 4;
}

static void ExpectPixel(const Host& host, uint32_t x, uint32_t y, unsigned r, unsigned g,
                         unsigned b, unsigned tolerance = 3)
{
    const uint8_t* pixel = Pixel(host, x, y);
    CHECK(std::abs((int)pixel[0] - (int)r) <= (int)tolerance);
    CHECK(std::abs((int)pixel[1] - (int)g) <= (int)tolerance);
    CHECK(std::abs((int)pixel[2] - (int)b) <= (int)tolerance);
}

static bl_UiElement Element(bl_UiContext context, bl_UiElement root, const char* tag,
                             const char* x, const char* y, const char* width,
                             const char* height, const char* color)
{
    bl_UiElement element = {};
    CHECK(bl_createUiElement(context, String(tag), &element) == BL_OK);
    CHECK(bl_setUiProperty(element, String("position"), String("absolute")) == BL_OK);
    CHECK(bl_setUiProperty(element, String("left"), String(x)) == BL_OK);
    CHECK(bl_setUiProperty(element, String("top"), String(y)) == BL_OK);
    CHECK(bl_setUiProperty(element, String("width"), String(width)) == BL_OK);
    CHECK(bl_setUiProperty(element, String("height"), String(height)) == BL_OK);
    if (color)
    {
        CHECK(bl_setUiProperty(element, String("background-color"), String(color)) == BL_OK);
    }
    CHECK(bl_appendUiChild(root, element) == BL_OK);
    return element;
}

struct EventState
{
    unsigned clicks;
    bl_UiElement element;
    bl_Status styleStatus;
    bl_Status disposeStatus;
    bl_Status textStatus;
};

static void Click(void* user, const bl_UiEvent* event)
{
    EventState* state = (EventState*)user;
    ++state->clicks;
    CHECK(event->kind == BL_UI_EVENT_CLICK);
    CHECK(event->currentTarget._id == state->element._id);
    state->styleStatus = bl_setUiProperty(state->element, String("background-color"),
                                         String("#00ff00"));
    state->disposeStatus = bl_disposeUiElement(state->element);
    state->textStatus = bl_setUiText(state->element, String("OK"));
}

static std::vector<uint8_t> Font()
{
    FILE* file = nullptr;
    CHECK(fopen_s(&file, LITE_UI_FONT_FILE, "rb") == 0 && file);
    if (!file)
    {
        return {};
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::rewind(file);
    std::vector<uint8_t> bytes((size_t)size);
    CHECK(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    std::fclose(file);
    return bytes;
}

static double InsideGaussian(unsigned coordinate, unsigned extent, unsigned spread, double sigma)
{
    double inside = 0;
    double normalization = 0;
    int radius = (int)std::ceil(3 * sigma);
    for (int offset = -radius; offset <= radius; ++offset)
    {
        double weight = std::exp(-0.5 * offset * offset / (sigma * sigma));
        int sample = (int)coordinate + offset;
        sample = sample < 0 ? 0 : sample >= (int)extent ? (int)extent - 1 : sample;
        if (sample >= (int)spread && sample < (int)(extent - spread))
        {
            inside += weight;
        }
        normalization += weight;
    }
    return inside / normalization;
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l)
{
    return DefWindowProcW(window, message, w, l);
}

int main(int argc, char** argv)
{
    bool visible = argc > 1 && std::strcmp(argv[1], "--visible") == 0;
    Host host = {};
    host.width = 320;
    host.height = 240;
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"LiteRetainedUiGpuTest";
    RegisterClassW(&windowClass);
    host.window = CreateWindowW(windowClass.lpszClassName, L"LiteLayer retained RmlUI / bgfx",
                                WS_OVERLAPPEDWINDOW, 80, 80, 640, 480, nullptr, nullptr,
                                windowClass.hInstance, nullptr);
    CHECK(host.window != nullptr);
    if (visible)
    {
        ShowWindow(host.window, SW_SHOW);
    }
    bgfx::Init init;
    CaptureCallback captures;
    init.callback = &captures;
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = host.window;
    init.swapChain.width = host.width;
    init.swapChain.height = host.height;
    CHECK(bgfx::init(init));
    host.initialized = true;
    std::printf("Initialized renderer %s, maxViews %u\n",
                bgfx::getRendererName(bgfx::getRendererType()),
                bgfx::getCaps()->limits.maxViews);
    host.color = bgfx::createTexture2D((uint16_t)host.width, (uint16_t)host.height, false, 1,
                                      bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
    host.depth = bgfx::createTexture2D((uint16_t)host.width, (uint16_t)host.height, false, 1,
                                      bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
    host.staging = bgfx::createTexture2D(
        (uint16_t)host.width, (uint16_t)host.height, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    const bgfx::TextureHandle attachments[] = {host.color, host.depth};
    host.target = bgfx::createFrameBuffer(2, attachments, false);
    Memory memory = {};
    Compiler compilerState = {};
    compilerState.inner = bl_shaderCompilerService();
    bl_ShaderCompilerService compiler = {&compilerState, Compile, Release};
    bl_RuntimeOptions runtimeOptions = {};
    runtimeOptions.allocator = {&memory, Allocate, Deallocate};
    runtimeOptions.onError = Error;
    runtimeOptions.shaderCompiler = &compiler;
    bl_Runtime* runtime = nullptr;
    CHECK(bl_createRuntime(&runtimeOptions, &runtime) == BL_OK);
    bl_NativeEngineOptions native = {};
    native.ownership = BL_BGFX_BORROWED;
    native.backend = BL_RENDERER_D3D11;
    native.target = {BL_TARGET_BGFX_FRAMEBUFFER, host.target.idx, host.width, host.height,
                     BL_COLOR_RGBA8, BL_DEPTH_D24S8, 1, 0, 8};
    native.hostUserData = &host;
    native.isExternalBgfxInitialized = Initialized;
    native.waitForSubmittedWork = Readback;
    bl_EngineContext engine = {};
    CHECK(bl_createEngine(runtime, &native, nullptr, &engine) == BL_OK);
    bl_SceneContext scene = {};
    CHECK(bl_createSceneContext(engine, &scene) == BL_OK);
    bl_SceneProperties sceneProperties = {};
    CHECK(bl_getSceneProperties(scene, &sceneProperties) == BL_OK);
    sceneProperties.clearColor = {0, 0, 1, 1};
    bl_FreeCamera camera = {};
    CHECK(bl_createFreeCamera(runtime, {0, 0, -3}, {0, 0, 0}, &camera) == BL_OK);
    sceneProperties.camera = camera;
    CHECK(bl_setSceneProperties(scene, &sceneProperties) == BL_OK);
    const bl_VertexSemantic attributes[] = {BL_ATTRIBUTE_POSITION};
    bl_ShaderMaterialOptions materialOptions = {};
    materialOptions.name = String("UI composition 3D box");
    materialOptions.vertexSource = String(
        "@vertex fn mainVertex(input: VertexInput) -> @builtin(position) vec4f { "
        "return shaderSystem.worldViewProjection * vec4f(input.position, 1.0); }");
    materialOptions.fragmentSource = String(
        "@fragment fn mainFragment() -> @location(0) vec4f { return vec4f(1.0,0.5,0.0,1.0); }");
    materialOptions.attributes = attributes;
    materialOptions.attributeCount = 1;
    const bl_ShaderUniformDecl system[] = {
        {{"worldViewProjection", 19}, BL_UNIFORM_MAT4, {}, true}};
    materialOptions.uniforms = system;
    materialOptions.uniformCount = 1;
    bl_ShaderMaterial material = {};
    CHECK(bl_createShaderMaterial(runtime, &materialOptions, &material) == BL_OK);
    bl_Mesh box = {};
    CHECK(bl_createBox(engine, nullptr, &box) == BL_OK);
    bl_MeshProperties meshProperties = {};
    CHECK(bl_getMeshProperties(box, &meshProperties) == BL_OK);
    meshProperties.material = material;
    CHECK(bl_setMeshProperties(box, &meshProperties) == BL_OK);
    bl_SceneNode node = {};
    CHECK(bl_meshNode(box, &node) == BL_OK);
    CHECK(bl_setNodePosition(node, {0, -1, 0}) == BL_OK);
    CHECK(bl_addToScene(scene, node) == BL_OK);
    CHECK(bl_registerScene(scene) == BL_OK);
    bl_UiContextOptions uiOptions = {};
    uiOptions.target = native.target;
    uiOptions.target.firstViewId = 16;
    uiOptions.target.viewCount = 8;
    uiOptions.densityRatio = 1;
    bl_UiContext ui = {};
    bl_Status created = bl_createUiContext(engine, &uiOptions, &ui);
    CHECK(created == BL_OK);
    if (created != BL_OK)
    {
        bl_disposeRuntime(runtime);
        bgfx::shutdown();
        DestroyWindow(host.window);
        return 1;
    }
    CHECK(bl_renderUi(ui) == BL_NOT_READY);
    CHECK(bl_disposeEngine(engine) == BL_BUSY);
    bl_UiContext invalid = {};
    CHECK(bl_createUiContext(engine, &uiOptions, &invalid) == BL_BUSY);
    bl_NativeEngineOptions conflict = native;
    conflict.target.firstViewId = 16;
    bl_EngineContext conflictEngine = {};
    CHECK(bl_createEngine(runtime, &conflict, nullptr, &conflictEngine) == BL_BUSY);
    bl_UiElement root = {};
    CHECK(bl_getUiRoot(ui, &root) == BL_OK);
    CHECK(bl_disposeUiElement(root) == BL_BUSY);
    std::vector<uint8_t> font = Font();
    CHECK(bl_loadUiFont(ui, {font.data(), font.size()}, String("Lato"), 400, false, false) == BL_OK);
    CHECK(bl_setUiProperty(root, String("font-family"), String("Lato")) == BL_OK);
    CHECK(bl_setUiProperty(root, String("font-size"), String("16px")) == BL_OK);
    bl_UiElement red = Element(ui, root, "div", "10px", "10px", "40px", "40px", "#ff0000");
    bl_UiElement alpha = Element(ui, root, "div", "60px", "10px", "40px", "40px",
                                 "rgba(255,255,0,128)");
    bl_UiElement gradient = Element(ui, root, "div", "110px", "10px", "80px", "40px", nullptr);
    CHECK(bl_setUiProperty(gradient, String("decorator"),
                           String("linear-gradient(to right, #ff0000, #0000ff)")) == BL_OK);
    bl_UiElement radial = Element(ui, root, "div", "210px", "10px", "60px", "60px", nullptr);
    CHECK(bl_setUiProperty(radial, String("decorator"),
                           String("radial-gradient(circle, #ffffff, #000000)")) == BL_OK);
    const uint8_t imagePixels[] = {0, 255, 0, 255, 0, 255, 0, 255,
                                   0, 255, 0, 255, 0, 255, 0, 255};
    CHECK(bl_registerUiImage(ui, String("green"), {imagePixels, sizeof(imagePixels)}, 2, 2) == BL_OK);
    bl_UiElement image = Element(ui, root, "img", "10px", "80px", "40px", "40px", nullptr);
    CHECK(bl_setUiAttribute(image, String("src"), String("green")) == BL_OK);
    const uint8_t samplingPixels[] = {255, 0, 0, 255, 0, 0, 255, 255,
                                      255, 0, 0, 255, 0, 0, 255, 255};
    CHECK(bl_registerUiImage(ui, String("point"), {samplingPixels, sizeof(samplingPixels)}, 2, 2) ==
          BL_OK);
    CHECK(bl_registerUiImage(ui, String("linear"), {samplingPixels, sizeof(samplingPixels)}, 2, 2) ==
          BL_OK);
    CHECK(bl_setUiImageSampling(ui, String("unknown"), true) == BL_INVALID_ARGUMENT);
    CHECK(bl_setUiImageSampling(ui, {nullptr, 1}, true) == BL_INVALID_ARGUMENT);
    CHECK(bl_setUiImageSampling(ui, String("point"), false) == BL_OK);
    CHECK(bl_setUiImageSampling(ui, String("point"), true) == BL_OK);
    bl_UiElement pointImage = Element(ui, root, "img", "10px", "195px", "40px", "40px", nullptr);
    bl_UiElement linearImage = Element(ui, root, "img", "60px", "195px", "40px", "40px", nullptr);
    CHECK(bl_setUiAttribute(pointImage, String("src"), String("point")) == BL_OK);
    CHECK(bl_setUiAttribute(linearImage, String("src"), String("linear")) == BL_OK);

    for (size_t fail = 1; fail <= 3; ++fail)
    {
        memory.failAt = memory.calls + fail;
        CHECK(bl_registerUiImage(ui, String("registration-rollback"),
                                 {samplingPixels, sizeof(samplingPixels)}, 2, 2) ==
              BL_OUT_OF_MEMORY);
        memory.failAt = 0;
        CHECK(bl_setUiImageSampling(ui, String("registration-rollback"), true) == BL_INVALID_ARGUMENT);
        CHECK(bl_registerUiImage(ui, String("registration-rollback"),
                                 {samplingPixels, sizeof(samplingPixels)}, 2, 2) == BL_OK);
        CHECK(bl_setUiImageSampling(ui, String("registration-rollback"), true) == BL_OK);
        CHECK(bl_unregisterUiImage(ui, String("registration-rollback")) == BL_OK);
    }
    bl_UiElement clipped = Element(ui, root, "div", "60px", "80px", "40px", "40px", nullptr);
    CHECK(bl_setUiProperty(clipped, String("overflow"), String("hidden")) == BL_OK);
    CHECK(bl_setUiProperty(clipped, String("clip"), String("always")) == BL_OK);
    bl_UiElement child = Element(ui, clipped, "div", "20px", "0px", "40px", "40px", "#ff0000");
    bl_UiElement transformed = Element(ui, root, "div", "120px", "80px", "30px", "30px", "#ff0000");
    CHECK(bl_setUiProperty(transformed, String("transform"), String("translateX(20px)")) == BL_OK);
    bl_UiElement rounded = Element(ui, root, "div", "210px", "80px", "40px", "40px", nullptr);
    CHECK(bl_setUiProperty(rounded, String("clip"), String("always")) == BL_OK);
    CHECK(bl_setUiProperty(rounded, String("border-radius"), String("12px")) == BL_OK);
    CHECK(bl_setUiProperty(rounded, String("overflow"), String("hidden")) == BL_OK);
    bl_UiElement masked = Element(ui, rounded, "div", "-5px", "-5px", "60px", "60px", "#00ff00");
    bl_UiElement dp = Element(ui, root, "div", "280px", "180px", "10dp", "10dp", "#ff0000");
    bl_UiElement text = Element(ui, root, "div", "10px", "150px", "280px", "32px", nullptr);
    CHECK(bl_setUiProperty(text, String("color"), String("#ffffff")) == BL_OK);
    CHECK(bl_setUiText(text, String("Retained RmlUI / bgfx <plain>")) == BL_OK);
    CHECK(bl_updateUi(ui, 0) == BL_OK);
    CHECK(bl_renderFrame(engine, 16) == BL_OK);
    CHECK(bl_renderUi(ui) == BL_OK);
    CHECK(Readback(&host) == BL_OK);
    Capture(host, "ui-initial.ppm");
    ExpectPixel(host, 20, 20, 255, 0, 0);
    ExpectPixel(host, 80, 20, 128, 128, 127, 4);
    ExpectPixel(host, 20, 90, 0, 255, 0);
    ExpectPixel(host, 85, 90, 255, 0, 0);
    ExpectPixel(host, 105, 90, 0, 0, 255);
    ExpectPixel(host, 125, 90, 0, 0, 255);
    ExpectPixel(host, 145, 90, 255, 0, 0);
    ExpectPixel(host, 230, 100, 0, 255, 0);
    ExpectPixel(host, 210, 80, 0, 0, 255);
    ExpectPixel(host, 295, 185, 0, 0, 255);
    ExpectPixel(host, 160, 210, 255, 128, 0, 4);
    ExpectPixel(host, 26, 210, 255, 0, 0);
    ExpectPixel(host, 33, 210, 0, 0, 255);
    ExpectPixel(host, 76, 210, 172, 0, 83, 4);
    ExpectPixel(host, 83, 210, 83, 0, 172, 4);
    CHECK(bl_setUiImageSampling(ui, String("point"), true) == BL_BUSY);
    CHECK(bl_setUiImageSampling(ui, String("point"), false) == BL_BUSY);
    CHECK(bl_setUiImageSampling(ui, String("linear"), true) == BL_BUSY);
    CHECK(Pixel(host, 115, 25)[0] > 220);
    CHECK(Pixel(host, 185, 25)[2] > 220);
    CHECK(Pixel(host, 240, 40)[0] > 220);
    CHECK(Pixel(host, 215, 15)[0] < 120);
    unsigned textPixels = 0;
    for (unsigned y = 150; y < 185; ++y)
    {
        for (unsigned x = 10; x < 300; ++x)
        {
            const uint8_t* p = Pixel(host, x, y);
            if (p[0] > 40 && p[1] > 40 && p[2] > 40)
            {
                ++textPixels;
            }
        }
    }
    CHECK(textPixels > 100);
    bl_UiStats before = {};
    CHECK(bl_getUiStats(ui, &before) == BL_OK);
    std::printf("Initial drawCount %llu, compiled geometry %llu, textures %llu\n",
                (unsigned long long)before.drawCount,
                (unsigned long long)before.geometryCompileCount,
                (unsigned long long)before.textureCreateCount);
    for (unsigned i = 1; i <= 30; ++i)
    {
        CHECK(bl_setUiText(text, String("Retained RmlUI / bgfx <plain>")) == BL_OK);
        CHECK(bl_updateUi(ui, i / 60.0) == BL_OK);
        CHECK(bl_renderFrame(engine, 16) == BL_OK);
        CHECK(bl_renderUi(ui) == BL_OK);
        bgfx::frame();
    }
    bl_UiStats after = {};
    CHECK(bl_getUiStats(ui, &after) == BL_OK);
    CHECK(before.geometryCompileCount == after.geometryCompileCount);
    CHECK(before.textureCreateCount == after.textureCreateCount);
    CHECK(before.uploadedBytes == after.uploadedBytes);
    host.failWait = true;
    CHECK(bl_disposeUiContext(ui) == BL_HOST_ERROR);
    host.failWait = false;
    bl_UiStats afterFailedWait = {};
    CHECK(bl_getUiStats(ui, &afterFailedWait) == BL_OK);
    CHECK(afterFailedWait.liveElementCount == after.liveElementCount);
    CHECK(afterFailedWait.liveTextureCount == after.liveTextureCount);
    CHECK(afterFailedWait.liveGeometryCount == after.liveGeometryCount);
    CHECK(bl_setUiImageSampling(ui, String("point"), false) == BL_BUSY);
    unsigned mutationWaits = host.waits;
    CHECK(bl_setUiProperty(red, String("background-color"), String("#00ff00")) == BL_OK);
    CHECK(bl_setUiText(text, String("Mutation: green, text and transformed retained geometry")) == BL_OK);
    CHECK(bl_updateUi(ui, 1) == BL_OK);
    CHECK(bl_renderFrame(engine, 16) == BL_OK);
    CHECK(bl_renderUi(ui) == BL_OK);
    CHECK(host.waits == mutationWaits);
    CHECK(Readback(&host) == BL_OK);
    ExpectPixel(host, 20, 20, 0, 255, 0);
    Capture(host, "ui-mutated.ppm");
    CHECK(bl_setUiProperty(red, String("mix-blend-mode"), String("difference")) == BL_UNSUPPORTED);
    CHECK(bl_updateUi(ui, 0.5) == BL_INVALID_ARGUMENT);
    CHECK(bl_appendUiChild(root, red) == BL_INVALID_ARGUMENT);
    CHECK(bl_appendUiChild(child, clipped) == BL_INVALID_ARGUMENT);
    CHECK(bl_unregisterUiImage(ui, String("green")) == BL_BUSY);
    EventState events = {};
    events.element = red;
    bl_UiListenerToken token = {};
    CHECK(bl_addUiEventListener(red, BL_UI_EVENT_CLICK, Click, &events, &token) == BL_OK);
    bl_UiInput input = {};
    input.kind = BL_UI_POINTER_MOVE;
    input.x = 20;
    input.y = 20;
    bool consumed = false;
    CHECK(bl_processUiInput(ui, &input, &consumed) == BL_OK);
    input.kind = BL_UI_POINTER_DOWN;
    CHECK(bl_processUiInput(ui, &input, &consumed) == BL_OK);
    input.kind = BL_UI_POINTER_UP;
    CHECK(bl_processUiInput(ui, &input, &consumed) == BL_OK);
    CHECK(events.clicks == 1);
    CHECK(events.styleStatus == BL_OK);
    CHECK(events.disposeStatus == BL_BUSY);
    CHECK(events.textStatus == BL_OK);
    CHECK(bl_removeUiEventListener(red, token) == BL_OK);
    CHECK(compilerState.compiles == compilerState.releases);
    auto wrongThread = std::async(std::launch::async, [ui]
    {
        bl_UiStats stats = {};
        return bl_getUiStats(ui, &stats);
    });
    CHECK(wrongThread.get() == BL_WRONG_THREAD);
    size_t plateau = 0;
    size_t externalPlateau = 0;
    for (unsigned round = 0; round < 3; ++round)
    {
        std::vector<bl_UiElement> elements(1000);
        for (bl_UiElement& element : elements)
        {
            CHECK(bl_createUiElement(ui, String("div"), &element) == BL_OK);
        }
        for (bl_UiElement element : elements)
        {
            CHECK(bl_disposeUiElement(element) == BL_OK);
            CHECK(bl_setUiText(element, String("stale")) == BL_DISPOSED);
        }
        bl_UiElement collect = {};
        CHECK(bl_createUiElement(ui, String("div"), &collect) == BL_OK);
        CHECK(bl_disposeUiElement(collect) == BL_OK);
        if (round == 1)
        {
            plateau = memory.live;
            externalPlateau = PrivateBytes();
            CHECK(externalPlateau != 0);
        }
        if (round == 2)
        {
            CHECK(memory.live == plateau);
            size_t currentPrivate = PrivateBytes();
            CHECK(currentPrivate != 0);
            CHECK(currentPrivate <= externalPlateau + 2 * 1024 * 1024);
        }
    }
    memory.failAt = memory.calls + 1;
    bl_UiElement failed = {};
    CHECK(bl_createUiElement(ui, String("div"), &failed) == BL_OUT_OF_MEMORY);
    memory.failAt = 0;
    CHECK(bl_setUiViewport(ui, 320, 220, 2) == BL_OK);
    CHECK(bl_updateUi(ui, 2) == BL_OK);
    CHECK(bl_renderFrame(engine, 16) == BL_OK);
    CHECK(bl_renderUi(ui) == BL_OK);
    CHECK(Readback(&host) == BL_OK);
    Capture(host, "ui-resized-dpi.ppm");
    ExpectPixel(host, 295, 185, 255, 0, 0);
    CHECK(bl_setUiViewport(ui, 0, 240, 1) == BL_INVALID_ARGUMENT);

    bl_UiContextOptions secondOptions = uiOptions;
    secondOptions.target.firstViewId = 32;
    bl_UiContext second = {};
    CHECK(bl_createUiContext(engine, &secondOptions, &second) == BL_OK);
    bl_UiElement secondRoot = {};
    CHECK(bl_getUiRoot(second, &secondRoot) == BL_OK);
    CHECK(bl_setUiProperty(secondRoot, String("font-family"), String("Lato")) == BL_OK);
    bl_UiElement secondElement = Element(second, secondRoot, "div", "260px", "80px", "40px",
                                         "30px", "#ffff00");
    CHECK(bl_setUiText(secondElement, String("Two")) == BL_OK);
    bl_UiElement detached = {};
    CHECK(bl_createUiElement(second, String("div"), &detached) == BL_OK);
    CHECK(bl_appendUiChild(root, detached) == BL_WRONG_ENGINE);
    CHECK(bl_updateUi(second, 2) == BL_OK);
    CHECK(bl_renderFrame(engine, 16) == BL_OK);
    CHECK(bl_renderUi(ui) == BL_OK);
    CHECK(bl_renderUi(second) == BL_OK);
    CHECK(Readback(&host) == BL_OK);
    ExpectPixel(host, 290, 105, 255, 255, 0);
    CHECK(bl_disposeUiContext(second) == BL_OK);
    CHECK(bl_setUiText(secondElement, String("disposed second")) == BL_DISPOSED);
    CHECK(bl_updateUi(ui, 2) == BL_OK);
    CHECK(bl_renderFrame(engine, 16) == BL_OK);
    CHECK(bl_renderUi(ui) == BL_OK);
    CHECK(Readback(&host) == BL_OK);
    ExpectPixel(host, 290, 105, 0, 0, 255);

    bl_Runtime* otherRuntime = nullptr;
    CHECK(bl_createRuntime(&runtimeOptions, &otherRuntime) == BL_OK);
    bl_NativeEngineOptions otherNative = native;
    otherNative.target.firstViewId = 64;
    bl_EngineContext otherEngine = {};
    CHECK(bl_createEngine(otherRuntime, &otherNative, nullptr, &otherEngine) == BL_OK);
    bl_UiContextOptions otherOptions = uiOptions;
    otherOptions.target.firstViewId = 80;
    bl_UiContext otherContext = {};
    CHECK(bl_createUiContext(otherEngine, &otherOptions, &otherContext) == BL_OK);
    bl_UiElement otherRoot = {};
    CHECK(bl_getUiRoot(otherContext, &otherRoot) == BL_OK);
    CHECK(bl_appendUiChild(root, otherRoot) == BL_WRONG_RUNTIME);
    CHECK(bl_disposeRuntime(otherRuntime) == BL_OK);

    for (unsigned phase = 0; phase < 4; ++phase)
    {
        FaultIo io = {};
        io.failRead = phase == 0;
        io.failDecode = phase == 1;
        io.invalidStride = phase == 2;
        for (unsigned pixel = 0; pixel < 4; ++pixel)
        {
            io.pixels[pixel * 4] = 255;
            io.pixels[pixel * 4 + 3] = 128;
        }
        bl_IOService service = {&io, ReadImageFile, nullptr, DecodeImage, ReleaseImageBuffer,
                                ReleaseDecodedImage};
        bl_RuntimeOptions ioOptions = runtimeOptions;
        ioOptions.io = &service;
        bl_Runtime* ioRuntime = nullptr;
        CHECK(bl_createRuntime(&ioOptions, &ioRuntime) == BL_OK);
        io.runtime = ioRuntime;
        bl_EngineContext ioEngine = {};
        CHECK(bl_createEngine(ioRuntime, &otherNative, nullptr, &ioEngine) == BL_OK);
        bl_UiContext ioContext = {};
        CHECK(bl_createUiContext(ioEngine, &otherOptions, &ioContext) == BL_OK);
        bl_UiElement ioRoot = {};
        CHECK(bl_getUiRoot(ioContext, &ioRoot) == BL_OK);
        bl_UiElement ioImage = Element(ioContext, ioRoot, "img", "5px", "125px", "20px", "20px",
                                        nullptr);
        CHECK(bl_setUiAttribute(ioImage, String("src"), String("service-image")) == BL_OK);
        bl_Status ioStatus = bl_updateUi(ioContext, 2);
        if (ioStatus == BL_OK)
        {
            ioStatus = bl_renderUi(ioContext);
        }
        bl_Status expected = phase == 2 ? BL_INVALID_ARGUMENT : phase < 2 ? BL_HOST_ERROR : BL_OK;
        CHECK(ioStatus == expected);
        CHECK(io.reads == 1);
        CHECK(io.buffersReleased == 1);
        CHECK(io.decoded == (phase == 0 ? 0u : 1u));
        CHECK(io.imagesReleased == io.decoded);
        CHECK(io.reentryStatus == BL_BUSY);
        if (phase < 3)
        {
            CHECK(bl_renderUi(ioContext) == expected);
        }
        else
        {
            CHECK(Readback(&host) == BL_OK);
            ExpectPixel(host, 10, 130, 128, 0, 127, 4);
        }
        CHECK(bl_disposeRuntime(ioRuntime) == BL_OK);
    }

    compilerState.invalidReflection = true;
    CHECK(bl_createUiContext(engine, &secondOptions, &second) == BL_SHADER_ERROR);
    compilerState.invalidReflection = false;
    compilerState.fail = true;
    CHECK(bl_createUiContext(engine, &secondOptions, &second) == BL_SHADER_ERROR);
    compilerState.fail = false;
    for (size_t fail = 1; fail <= 12; ++fail)
    {
        memory.failAt = memory.calls + fail;
        bl_Status failStatus = bl_createUiContext(engine, &secondOptions, &second);
        memory.failAt = 0;
        if (failStatus == BL_OK)
        {
            CHECK(bl_disposeUiContext(second) == BL_OK);
            break;
        }
        CHECK(failStatus == BL_OUT_OF_MEMORY);
    }
    CHECK(compilerState.compiles == compilerState.releases);

    CHECK(bl_createUiContext(engine, &secondOptions, &second) == BL_OK);
    CHECK(bl_getUiRoot(second, &secondRoot) == BL_OK);
    bl_UiElement missing = Element(second, secondRoot, "img", "0px", "0px", "20px", "20px", nullptr);
    CHECK(bl_setUiAttribute(missing, String("src"), String("missing-no-io")) == BL_OK);
    bl_Status missingStatus = bl_updateUi(second, 2);
    if (missingStatus == BL_OK)
    {
        missingStatus = bl_renderUi(second);
    }
    CHECK(missingStatus == BL_NOT_READY);
    CHECK(bl_renderUi(second) == BL_NOT_READY);
    CHECK(bl_disposeUiContext(second) == BL_OK);

    CHECK(bl_createUiContext(engine, &secondOptions, &second) == BL_OK);
    CHECK(bl_getUiRoot(second, &secondRoot) == BL_OK);
    bl_UiElement filtered = Element(second, secondRoot, "div", "0px", "0px", "20px", "20px",
                                     "#ff0000");
    CHECK(bl_setUiProperty(filtered, String("filter"), String("hue-rotate(30deg)")) == BL_OK);
    bl_Status filterStatus = bl_updateUi(second, 2);
    if (filterStatus == BL_OK)
    {
        filterStatus = bl_renderUi(second);
    }
    CHECK(filterStatus == BL_UNSUPPORTED);
    CHECK(bl_renderUi(second) == BL_UNSUPPORTED);
    CHECK(bl_disposeUiContext(second) == BL_OK);

    CHECK(bl_createUiContext(engine, &secondOptions, &second) == BL_OK);
    CHECK(bl_getUiRoot(second, &secondRoot) == BL_OK);
    CHECK(bl_setUiWhiteDifference(second, true) == BL_OK);
    bl_UiElement difference = Element(second, secondRoot, "div", "0px", "0px", "20px", "20px",
                                       "#ffffff");
    const unsigned backdrops[][3] = {{0, 0, 0}, {255, 255, 255}, {32, 128, 224}};
    const unsigned alphas[] = {255, 128, 229};
    for (const auto& backdrop : backdrops)
    {
        bl_SceneProperties backdropProperties = sceneProperties;
        backdropProperties.clearColor = {backdrop[0] / 255.0, backdrop[1] / 255.0,
                                          backdrop[2] / 255.0, 1};
        CHECK(bl_setSceneProperties(scene, &backdropProperties) == BL_OK);
        for (unsigned alphaByte : alphas)
        {
            char color[16];
            std::snprintf(color, sizeof(color), "#ffffff%02x", alphaByte);
            CHECK(bl_setUiProperty(difference, String("background-color"), String(color)) == BL_OK);
            CHECK(bl_updateUi(second, 2) == BL_OK);
            CHECK(bl_renderFrame(engine, 16) == BL_OK);
            CHECK(bl_renderUi(second) == BL_OK);
            CHECK(Readback(&host) == BL_OK);
            unsigned expected[3] = {};
            double alphaValue = alphaByte / 255.0;
            for (unsigned channel = 0; channel < 3; ++channel)
            {
                expected[channel] = (unsigned)std::lround(
                    alphaByte + (1 - 2 * alphaValue) * backdrop[channel]);
            }
            ExpectPixel(host, 10, 10, expected[0], expected[1], expected[2], 3);
            CHECK(Pixel(host, 10, 10)[3] == 255);
        }
    }
    CHECK(bl_setUiProperty(difference, String("background-color"), String("#ffffff")) == BL_OK);
    CHECK(bl_setUiProperty(difference, String("opacity"), String("0.9")) == BL_OK);
    CHECK(bl_updateUi(second, 2) == BL_OK);
    CHECK(bl_renderFrame(engine, 16) == BL_OK);
    CHECK(bl_renderUi(second) == BL_OK);
    CHECK(Readback(&host) == BL_OK);
    ExpectPixel(host, 10, 10, 204, 127, 51, 3);
    CHECK(bl_setUiProperty(difference, String("background-color"), String("#ff0000")) == BL_OK);
    CHECK(bl_updateUi(second, 2) == BL_OK);
    CHECK(bl_renderUi(second) == BL_UNSUPPORTED);
    CHECK(bl_disposeUiContext(second) == BL_OK);
    CHECK(bl_setSceneProperties(scene, &sceneProperties) == BL_OK);

    for (unsigned colored = 0; colored < 2; ++colored)
    {
        CHECK(bl_createUiContext(engine, &secondOptions, &second) == BL_OK);
        CHECK(bl_getUiRoot(second, &secondRoot) == BL_OK);
        CHECK(bl_setUiWhiteDifference(second, true) == BL_OK);
        bl_UiElement source = Element(second, secondRoot, colored ? "div" : "img", "0px", "0px",
                                       "20px", "20px", nullptr);
        if (colored)
        {
            CHECK(bl_setUiProperty(source, String("decorator"),
                                   String("linear-gradient(to right, #ff0000, #0000ff)")) == BL_OK);
        }
        else
        {
            CHECK(bl_registerUiImage(second, String("colored"),
                                     {samplingPixels, sizeof(samplingPixels)}, 2, 2) == BL_OK);
            CHECK(bl_setUiAttribute(source, String("src"), String("colored")) == BL_OK);
        }
        CHECK(bl_updateUi(second, 2) == BL_OK);
        CHECK(bl_renderUi(second) == BL_UNSUPPORTED);
        CHECK(bl_disposeUiContext(second) == BL_OK);
    }

    CHECK(bl_disposeUiElement(clipped) == BL_OK);
    CHECK(bl_setUiText(child, String("disposed subtree")) == BL_DISPOSED);
    CHECK(bl_disposeUiElement(alpha) == BL_OK);
    CHECK(bl_disposeUiElement(image) == BL_OK);
    CHECK(bl_disposeUiElement(rounded) == BL_OK);
    CHECK(bl_setUiText(masked, String("disposed rounded mask")) == BL_DISPOSED);
    CHECK(bl_disposeUiElement(dp) == BL_OK);
    if (visible)
    {
        bl_NativeTarget swapchain = native.target;
        swapchain.kind = BL_TARGET_SWAPCHAIN;
        swapchain.framebufferIndex = BL_INVALID_BGFX_HANDLE;
        CHECK(bl_setNativeTarget(engine, &swapchain) == BL_OK);
        bl_UiContextOptions sampleOptions = uiOptions;
        sampleOptions.target = swapchain;
        sampleOptions.target.firstViewId = 48;
        bl_UiContext sample = {};
        CHECK(bl_createUiContext(engine, &sampleOptions, &sample) == BL_OK);
        bl_UiElement sampleRoot = {};
        CHECK(bl_getUiRoot(sample, &sampleRoot) == BL_OK);
        CHECK(bl_setUiProperty(sampleRoot, String("font-family"), String("Lato")) == BL_OK);
        CHECK(bl_setUiProperty(sampleRoot, String("color"), String("#ffffff")) == BL_OK);
        bl_UiElement heading = Element(sample, sampleRoot, "div", "10px", "12px", "300px",
                                        "28px", nullptr);
        CHECK(bl_setUiText(heading, String("RmlUI / bgfx retained overlay")) == BL_OK);
        bl_UiElement panel = Element(sample, sampleRoot, "div", "25px", "48px", "270px", "76px",
                                      "#00000080");
        CHECK(bl_setUiProperty(panel, String("border-radius"), String("8px")) == BL_OK);
        bl_UiElement ramp = Element(sample, panel, "div", "8px", "8px", "54px", "54px", nullptr);
        CHECK(bl_setUiProperty(ramp, String("decorator"),
                               String("radial-gradient(circle, #ffffff, #0099ff)")) == BL_OK);
        bl_UiElement label = Element(sample, panel, "div", "76px", "12px", "180px", "52px",
                                      nullptr);
        CHECK(bl_setUiText(label, String("Fonts, gradients, alpha\nGPU UI after real 3D")) == BL_OK);
        MSG message = {};
        for (unsigned i = 0; i < 120; ++i)
        {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            CHECK(bl_updateUi(sample, 3 + i / 60.0) == BL_OK);
            CHECK(bl_renderFrame(engine, 16) == BL_OK);
            CHECK(bl_renderUi(sample) == BL_OK);
            if (i == 90)
            {
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "ui-visible-swapchain.ppm");
            }
            bgfx::frame();
            Sleep(8);
        }
        CHECK(bl_disposeUiContext(sample) == BL_OK);
        CHECK(captures.screenshots == 1);
        CHECK(captures.errors == 0);
        CHECK(bl_setNativeTarget(engine, &native.target) == BL_OK);
    }
    CHECK(bl_disposeUiContext(ui) == BL_OK);
    CHECK(bl_disposeUiContext(ui) == BL_OK);
    CHECK(bl_getUiStats(ui, &after) == BL_DISPOSED);
    CHECK(bl_setUiImageSampling(ui, String("point"), true) == BL_DISPOSED);

    Host shadowHost = {};
    shadowHost.window = host.window;
    shadowHost.initialized = true;
    shadowHost.width = 1280;
    shadowHost.height = 720;
    shadowHost.readbackView = (uint16_t)(bgfx::getCaps()->limits.maxViews - 1);
    shadowHost.color = bgfx::createTexture2D(1280, 720, false, 1, bgfx::TextureFormat::RGBA8,
                                             BGFX_TEXTURE_RT);
    shadowHost.depth = bgfx::createTexture2D(1280, 720, false, 1, bgfx::TextureFormat::D24S8,
                                             BGFX_TEXTURE_RT_WRITE_ONLY);
    shadowHost.staging = bgfx::createTexture2D(1280, 720, false, 1, bgfx::TextureFormat::RGBA8,
                                               BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    const bgfx::TextureHandle shadowAttachments[] = {shadowHost.color, shadowHost.depth};
    shadowHost.target = bgfx::createFrameBuffer(2, shadowAttachments, false);
    bl_Runtime* shadowRuntime = nullptr;
    CHECK(bl_createRuntime(&runtimeOptions, &shadowRuntime) == BL_OK);
    bl_NativeEngineOptions shadowNative = native;
    shadowNative.hostUserData = &shadowHost;
    shadowNative.target = {BL_TARGET_BGFX_FRAMEBUFFER, shadowHost.target.idx, 1280, 720,
                            BL_COLOR_RGBA8, BL_DEPTH_D24S8, 1, 128, 8};
    bl_EngineContext shadowEngine = {};
    CHECK(bl_createEngine(shadowRuntime, &shadowNative, nullptr, &shadowEngine) == BL_OK);
    bl_SceneContext shadowScene = {};
    CHECK(bl_createSceneContext(shadowEngine, &shadowScene) == BL_OK);
    bl_SceneProperties shadowProperties = {};
    CHECK(bl_getSceneProperties(shadowScene, &shadowProperties) == BL_OK);
    shadowProperties.clearColor = {0.5, 0.5, 0.5, 1};
    CHECK(bl_setSceneProperties(shadowScene, &shadowProperties) == BL_OK);
    CHECK(bl_registerScene(shadowScene) == BL_OK);
    bl_UiContextOptions shadowOptions = {};
    shadowOptions.target = shadowNative.target;
    shadowOptions.target.firstViewId = 160;
    shadowOptions.target.viewCount = 64;
    shadowOptions.densityRatio = 1;
    bl_UiContext shadowContext = {};
    CHECK(bl_createUiContext(shadowEngine, &shadowOptions, &shadowContext) == BL_OK);
    bl_UiElement shadowRoot = {};
    CHECK(bl_getUiRoot(shadowContext, &shadowRoot) == BL_OK);
    bl_UiElement underwater = Element(shadowContext, shadowRoot, "div", "0px", "0px", "1280px",
                                       "720px", nullptr);
    CHECK(bl_setUiProperty(
              underwater, String("decorator"),
              String("radial-gradient(ellipse at center, rgba(30,90,150,35%) 0%,"
                     "rgba(12,48,96,60%) 100%)")) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 0) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_OK);
    CHECK(Readback(&shadowHost) == BL_OK);
    std::vector<uint8_t> gradientBaseline = shadowHost.pixels;
    CHECK(bl_setUiProperty(underwater, String("box-shadow"),
                           String("inset 0 0 220px 60px rgba(4,26,60,85%)")) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 1) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_OK);
    CHECK(Readback(&shadowHost) == BL_OK);
    Capture(shadowHost, "ui-inset-shadow.ppm");
    ExpectPixel(shadowHost, 640, 360, 93, 114, 135, 5);
    const uint8_t* center = Pixel(shadowHost, 640, 360);
    const uint8_t* topEdge = Pixel(shadowHost, 640, 2);
    const uint8_t* innerEdge = Pixel(shadowHost, 640, 60);
    const uint8_t* farEdge = Pixel(shadowHost, 640, 250);
    CHECK(topEdge[0] < innerEdge[0]);
    CHECK(innerEdge[0] < farEdge[0]);
    CHECK(farEdge[0] < center[0]);
    CHECK(topEdge[3] == 255 && center[3] == 255);
    size_t topOffset = ((size_t)2 * 1280 + 640) * 4;
    CHECK(topEdge[0] + 10 < gradientBaseline[topOffset]);
    bl_UiStats shadowCold = {};
    CHECK(bl_getUiStats(shadowContext, &shadowCold) == BL_OK);
    CHECK(shadowCold.textureCreateCount >= 9);
    for (unsigned frame = 0; frame < 1000; ++frame)
    {
        CHECK(bl_updateUi(shadowContext, 1 + frame / 60.0) == BL_OK);
        CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
        CHECK(bl_renderUi(shadowContext) == BL_OK);
        bgfx::frame();
    }
    bl_UiStats shadowWarm = {};
    CHECK(bl_getUiStats(shadowContext, &shadowWarm) == BL_OK);
    CHECK(shadowWarm.textureCreateCount == shadowCold.textureCreateCount);
    CHECK(shadowWarm.geometryCompileCount == shadowCold.geometryCompileCount);
    CHECK(shadowWarm.uploadedBytes == shadowCold.uploadedBytes);
    CHECK(Readback(&shadowHost) == BL_OK);
    std::vector<uint8_t> singleShadowControl = shadowHost.pixels;
    bl_UiContextOptions duplicateOptions = shadowOptions;
    duplicateOptions.target.firstViewId = 256;
    bl_UiContext duplicateShadow = {};
    CHECK(bl_createUiContext(shadowEngine, &duplicateOptions, &duplicateShadow) == BL_OK);
    bl_UiElement duplicateRoot = {};
    CHECK(bl_getUiRoot(duplicateShadow, &duplicateRoot) == BL_OK);
    bl_UiElement duplicateElement = Element(duplicateShadow, duplicateRoot, "div", "0px", "0px",
                                             "1280px", "720px", nullptr);
    CHECK(bl_setUiProperty(
              duplicateElement, String("decorator"),
              String("radial-gradient(ellipse at center, rgba(30,90,150,35%) 0%,"
                     "rgba(12,48,96,60%) 100%)")) == BL_OK);
    CHECK(bl_setUiProperty(duplicateElement, String("box-shadow"),
                           String("inset 0 0 220px 60px rgba(4,26,60,85%)")) == BL_OK);
    CHECK(bl_updateUi(duplicateShadow, 18) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(duplicateShadow) == BL_OK);
    bl_UiStats duplicateStats = {};
    CHECK(bl_getUiStats(duplicateShadow, &duplicateStats) == BL_OK);
    CHECK(duplicateStats.drawCount >= 2);
    CHECK(Readback(&shadowHost) == BL_OK);
    CHECK(shadowHost.pixels == singleShadowControl);
    CHECK(bl_disposeUiContext(shadowContext) == BL_OK);
    CHECK(bl_updateUi(duplicateShadow, 18) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(duplicateShadow) == BL_OK);
    CHECK(Readback(&shadowHost) == BL_OK);
    CHECK(shadowHost.pixels == singleShadowControl);

    for (unsigned generation = 0; generation < 16; ++generation)
    {
        bl_UiContextOptions churnOptions = shadowOptions;
        churnOptions.target.firstViewId = 320;
        churnOptions.target.viewCount = 32;
        bl_UiContext churn = {};
        CHECK(bl_createUiContext(shadowEngine, &churnOptions, &churn) == BL_OK);
        bl_UiElement churnRoot = {};
        CHECK(bl_getUiRoot(churn, &churnRoot) == BL_OK);
        bl_UiElement churnElement = Element(churn, churnRoot, "div", "0px", "0px", "64px", "32px",
                                             nullptr);
        CHECK(bl_setUiProperty(churnElement, String("box-shadow"),
                               String("inset 0 0 8px 2px #ff0000")) == BL_OK);
        CHECK(bl_updateUi(churn, 18) == BL_OK);
        CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
        CHECK(bl_renderUi(churn) == BL_OK);
        CHECK(Readback(&shadowHost) == BL_OK);
        CHECK(bl_disposeUiContext(churn) == BL_OK);
        CHECK(bl_updateUi(duplicateShadow, 18) == BL_OK);
        CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
        CHECK(bl_renderUi(duplicateShadow) == BL_OK);
        CHECK(Readback(&shadowHost) == BL_OK);
        CHECK(shadowHost.pixels == singleShadowControl);
    }

    CHECK(bl_createUiContext(shadowEngine, &shadowOptions, &shadowContext) == BL_OK);
    CHECK(bl_getUiRoot(shadowContext, &shadowRoot) == BL_OK);
    underwater = Element(shadowContext, shadowRoot, "div", "0px", "0px", "1280px", "720px",
                          nullptr);
    CHECK(bl_setUiProperty(
              underwater, String("decorator"),
              String("radial-gradient(ellipse at center, rgba(30,90,150,35%) 0%,"
                     "rgba(12,48,96,60%) 100%)")) == BL_OK);
    CHECK(bl_setUiProperty(underwater, String("box-shadow"),
                           String("inset 0 0 220px 60px rgba(4,26,60,85%)")) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 18) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_OK);
    CHECK(Readback(&shadowHost) == BL_OK);
    CHECK(shadowHost.pixels == singleShadowControl);
    CHECK(bl_disposeUiContext(duplicateShadow) == BL_OK);
    std::printf("Shadow retained: cold textures %llu, warm textures %llu, cold draws %llu, "
                "warm draws %llu\n",
                (unsigned long long)shadowCold.textureCreateCount,
                (unsigned long long)shadowWarm.textureCreateCount,
                (unsigned long long)shadowCold.drawCount,
                (unsigned long long)shadowWarm.drawCount);

    CHECK(bl_setUiProperty(underwater, String("decorator"), String("none")) == BL_OK);
    CHECK(bl_setUiProperty(underwater, String("box-shadow"),
                           String("inset 0 0 220px 60px #ff0000")) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 18) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_OK);
    CHECK(Readback(&shadowHost) == BL_OK);
    const unsigned sampleYs[] = {2, 60, 110, 250, 360};
    for (unsigned y : sampleYs)
    {
        double alphaValue = 1 - InsideGaussian(640, 1280, 60, 110) *
                                    InsideGaussian(y, 720, 60, 110);
        unsigned redValue = (unsigned)std::lround(255 * alphaValue + 128 * (1 - alphaValue));
        unsigned other = (unsigned)std::lround(128 * (1 - alphaValue));
        ExpectPixel(shadowHost, 640, y, redValue, other, other, 4);
    }

    CHECK(bl_setUiProperty(underwater, String("opacity"), String("0")) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 19) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_OK);
    CHECK(Readback(&shadowHost) == BL_OK);
    ExpectPixel(shadowHost, 640, 2, 128, 128, 128);
    CHECK(bl_setUiProperty(underwater, String("opacity"), String("0.5")) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 20) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_OK);
    CHECK(Readback(&shadowHost) == BL_OK);
    double edgeAlpha = (127.0 / 255) *
                       (1 - InsideGaussian(640, 1280, 60, 110) *
                                InsideGaussian(2, 720, 60, 110));
    ExpectPixel(shadowHost, 640, 2,
                (unsigned)std::lround(255 * edgeAlpha + 128 * (1 - edgeAlpha)),
                (unsigned)std::lround(128 * (1 - edgeAlpha)),
                (unsigned)std::lround(128 * (1 - edgeAlpha)), 4);

    CHECK(bl_setUiProperty(underwater, String("opacity"), String("1")) == BL_OK);
    CHECK(bl_setUiProperty(underwater, String("width"), String("100%")) == BL_OK);
    CHECK(bl_setUiProperty(underwater, String("height"), String("100%")) == BL_OK);
    CHECK(bl_setUiProperty(underwater, String("border-radius"), String("4dp")) == BL_OK);
    CHECK(bl_setUiViewport(shadowContext, 960, 540, 2) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 21) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_OK);
    CHECK(Readback(&shadowHost) == BL_OK);
    CHECK(Pixel(shadowHost, 480, 2)[0] > Pixel(shadowHost, 480, 270)[0]);
    bl_UiStats resizedShadow = {};
    CHECK(bl_getUiStats(shadowContext, &resizedShadow) == BL_OK);
    CHECK(resizedShadow.textureCreateCount > shadowWarm.textureCreateCount);
    CHECK(resizedShadow.textureReleaseCount > shadowWarm.textureReleaseCount);
    CHECK(bl_updateUi(shadowContext, 22) == BL_OK);
    CHECK(bl_renderFrame(shadowEngine, 16) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_OK);
    bl_UiStats resizedWarm = {};
    CHECK(bl_getUiStats(shadowContext, &resizedWarm) == BL_OK);
    CHECK(resizedWarm.textureCreateCount == resizedShadow.textureCreateCount);
    CHECK(bl_disposeUiContext(shadowContext) == BL_OK);

    shadowOptions.target.viewCount = 4;
    CHECK(bl_createUiContext(shadowEngine, &shadowOptions, &shadowContext) == BL_OK);
    CHECK(bl_getUiRoot(shadowContext, &shadowRoot) == BL_OK);
    underwater = Element(shadowContext, shadowRoot, "div", "0px", "0px", "1280px", "720px",
                          nullptr);
    CHECK(bl_setUiProperty(underwater, String("box-shadow"),
                           String("inset 0 0 220px 60px #ff0000")) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 0) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_UNSUPPORTED);
    CHECK(bl_disposeUiContext(shadowContext) == BL_OK);

    shadowOptions.target.viewCount = 64;
    CHECK(bl_createUiContext(shadowEngine, &shadowOptions, &shadowContext) == BL_OK);
    CHECK(bl_getUiRoot(shadowContext, &shadowRoot) == BL_OK);
    CHECK(bl_setUiWhiteDifference(shadowContext, true) == BL_OK);
    underwater = Element(shadowContext, shadowRoot, "div", "0px", "0px", "1280px", "720px",
                          "#ffffff");
    CHECK(bl_setUiProperty(underwater, String("filter"), String("blur(2px)")) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 0) == BL_OK);
    CHECK(bl_renderUi(shadowContext) == BL_UNSUPPORTED);
    CHECK(bl_disposeUiContext(shadowContext) == BL_OK);

    CHECK(bl_createUiContext(shadowEngine, &shadowOptions, &shadowContext) == BL_OK);
    CHECK(bl_getUiRoot(shadowContext, &shadowRoot) == BL_OK);
    underwater = Element(shadowContext, shadowRoot, "div", "0px", "0px", "1280px", "720px",
                          nullptr);
    CHECK(bl_setUiProperty(underwater, String("box-shadow"),
                           String("inset 0 0 220px 60px #ff0000")) == BL_OK);
    CHECK(bl_updateUi(shadowContext, 0) == BL_OK);
    memory.failAt = memory.calls + 1;
    CHECK(bl_renderUi(shadowContext) == BL_OUT_OF_MEMORY);
    memory.failAt = 0;
    CHECK(bl_renderUi(shadowContext) == BL_OUT_OF_MEMORY);
    CHECK(bl_disposeUiContext(shadowContext) == BL_OK);
    CHECK(bl_disposeRuntime(shadowRuntime) == BL_OK);
    bgfx::destroy(shadowHost.target);
    bgfx::destroy(shadowHost.staging);
    bgfx::destroy(shadowHost.color);
    bgfx::destroy(shadowHost.depth);

    CHECK(bl_createUiContext(engine, &uiOptions, &ui) == BL_OK);
    CHECK(compilerState.compiles == compilerState.releases);
    CHECK(bl_disposeRuntime(runtime) == BL_OK);
    CHECK(memory.live == 0);
    CHECK(host.waits > 0);
    bgfx::destroy(host.target);
    bgfx::destroy(host.staging);
    bgfx::destroy(host.color);
    bgfx::destroy(host.depth);
    bgfx::frame();
    bgfx::shutdown();
    host.initialized = false;
    DestroyWindow(host.window);
    std::printf("Retained UI tests: %u failures; external RmlUI/FreeType allocations excluded; "
                "Core peak %zu bytes, %zu allocations\n", failures, memory.peak, memory.calls);
    return failures ? 1 : 0;
}
