#include "TestRuntime.h"
#include <babylon_lite_shader_compiler.h>
#include <bgfx/bgfx.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <future>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    bl_String String(std::string_view value)
    {
        return {value.data(), value.size()};
    }

    constexpr std::string_view Vertex = R"(
@vertex fn mainVertex(input: VertexInput) -> @builtin(position) vec4f {
    return shaderSystem.worldViewProjection * vec4f(input.position, 1.0);
})";
    constexpr std::string_view Fragment = R"(
@fragment fn mainFragment() -> @location(0) vec4f {
    return vec4f(shaderUniforms.tint, 1.0);
})";

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_CLOSE)
        {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    struct WindowHost
    {
        HWND window{};
        std::thread thread;

        void Start()
        {
            std::promise<HWND> started;
            auto ready = started.get_future();
            thread = std::thread([&started]
            {
                const wchar_t* name = L"BabylonLiteReadbackTest";
                WNDCLASSW windowClass{};
                windowClass.lpfnWndProc = WindowProcedure;
                windowClass.hInstance = GetModuleHandleW(nullptr);
                windowClass.lpszClassName = name;
                RegisterClassW(&windowClass);
                HWND window = CreateWindowW(name, L"Babylon Lite readback test", WS_OVERLAPPEDWINDOW,
                    CW_USEDEFAULT, CW_USEDEFAULT, 64, 64, nullptr, nullptr, windowClass.hInstance, nullptr);
                started.set_value(window);
                if (!window)
                {
                    return;
                }
                MSG message{};
                while (GetMessageW(&message, nullptr, 0, 0) > 0)
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            });
            window = ready.get();
        }

        ~WindowHost()
        {
            if (window)
            {
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
            if (thread.joinable())
            {
                thread.join();
            }
        }
    };

    class LiteNativeGraphics : public testing::Test
    {
    public:
        static bool ExternalInitialized(void* user)
        {
            return static_cast<LiteNativeGraphics*>(user)->initialized;
        }

        static bl_Status WaitSubmitted(void* user)
        {
            return static_cast<LiteNativeGraphics*>(user)->Readback() ? BL_OK : BL_HOST_ERROR;
        }

    protected:
        WindowHost window;
        bool initialized{};
        bgfx::TextureHandle color{BGFX_INVALID_HANDLE};
        bgfx::TextureHandle depth{BGFX_INVALID_HANDLE};
        bgfx::TextureHandle staging{BGFX_INVALID_HANDLE};
        bgfx::FrameBufferHandle target{BGFX_INVALID_HANDLE};
        std::array<uint8_t, 64 * 64 * 4> pixels{};
        bl_ShaderCompilerService compiler{bl_shaderCompilerService()};
        std::unique_ptr<TestRuntime> runtime;
        bl_EngineContext engine{};
        bl_SceneContext scene{};
        bl_ShaderMaterial material{};
        bl_Mesh mesh{};
        bl_SceneNode meshNode{};
        uint16_t readbackView{};

        void SetUp() override
        {
            window.Start();
            ASSERT_NE(window.window, nullptr);
            bgfx::Init init;
            init.type = bgfx::RendererType::Direct3D11;
            init.swapChain.nwh = window.window;
            init.swapChain.width = 64;
            init.swapChain.height = 64;
            ASSERT_TRUE(bgfx::init(init));
            initialized = true;
            ASSERT_NE(bgfx::getCaps(), nullptr);
            ASSERT_GT(bgfx::getCaps()->limits.maxViews, 32u);
            readbackView = static_cast<uint16_t>(bgfx::getCaps()->limits.maxViews - 1);
            color = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
            depth = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
            staging = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
            const bgfx::TextureHandle attachments[]{color, depth};
            target = bgfx::createFrameBuffer(2, attachments, false);
            ASSERT_TRUE(bgfx::isValid(color) && bgfx::isValid(depth) && bgfx::isValid(staging) && bgfx::isValid(target));

            runtime = std::make_unique<TestRuntime>(nullptr, &compiler);
            ASSERT_EQ(runtime->status, BL_OK);
            bl_NativeEngineOptions native{};
            native.ownership = BL_BGFX_BORROWED;
            native.backend = BL_RENDERER_D3D11;
            native.target = {BL_TARGET_BGFX_FRAMEBUFFER, target.idx, 64, 64, BL_COLOR_RGBA8, BL_DEPTH_D24S8, 1, 0, 8};
            native.hostUserData = this;
            native.isExternalBgfxInitialized = [](void* user)
            {
                return static_cast<LiteNativeGraphics*>(user)->initialized;
            };
            native.waitForSubmittedWork = [](void* user)
            {
                return static_cast<LiteNativeGraphics*>(user)->Readback() ? BL_OK : BL_HOST_ERROR;
            };
            ASSERT_EQ(bl_createEngine(runtime->runtime, &native, nullptr, &engine), BL_OK);
            ASSERT_EQ(bl_createSceneContext(engine, &scene), BL_OK);
            bl_FreeCamera camera{};
            ASSERT_EQ(bl_createFreeCamera(runtime->runtime, {0, 0, -3}, {0, 0, 0}, &camera), BL_OK);
            bl_CameraProperties cameraProperties{};
            ASSERT_EQ(bl_getCameraProperties(camera, &cameraProperties), BL_OK);
            cameraProperties.nearPlane = 0.1;
            cameraProperties.farPlane = 100;
            ASSERT_EQ(bl_setCameraProperties(camera, &cameraProperties), BL_OK);
            bl_SceneProperties properties{};
            ASSERT_EQ(bl_getSceneProperties(scene, &properties), BL_OK);
            properties.camera = camera;
            properties.clearColor = {0.05, 0.1, 0.2, 1};
            ASSERT_EQ(bl_setSceneProperties(scene, &properties), BL_OK);
        }

        void TearDown() override
        {
            runtime.reset();
            if (initialized)
            {
                if (bgfx::isValid(target)) bgfx::destroy(target);
                if (bgfx::isValid(staging)) bgfx::destroy(staging);
                if (bgfx::isValid(color)) bgfx::destroy(color);
                if (bgfx::isValid(depth)) bgfx::destroy(depth);
                bgfx::frame();
                bgfx::frame();
                bgfx::shutdown();
                initialized = false;
            }
        }

        bool Readback()
        {
            if (!bgfx::isValid(staging) || !bgfx::isValid(color))
            {
                return false;
            }
            bgfx::TextureRegion source{};
            source.handle = color;
            bgfx::TextureRegion destination{};
            destination.handle = staging;
            bgfx::blit(readbackView, destination, source);
            const uint32_t ready = bgfx::read(destination, pixels.data());
            for (uint32_t i = 0; i < 32; ++i)
            {
                if (bgfx::frame() >= ready)
                {
                    return true;
                }
            }
            return false;
        }

        void CreateColoredBox()
        {
            const bl_VertexSemantic attributes[]{BL_ATTRIBUTE_POSITION};
            const double tint[]{1, 0, 0};
            const bl_ShaderUniformDecl uniforms[]
            {
                {String("worldViewProjection"), BL_UNIFORM_MAT4, {}, true},
                {String("tint"), BL_UNIFORM_VEC3, {tint, 3}, false},
            };
            bl_ShaderMaterialOptions options{};
            options.name = String("readback-dynamic-WGSL");
            options.vertexSource = String(Vertex);
            options.fragmentSource = String(Fragment);
            options.attributes = attributes;
            options.attributeCount = 1;
            options.uniforms = uniforms;
            options.uniformCount = 2;
            ASSERT_EQ(bl_createShaderMaterial(runtime->runtime, &options, &material), BL_OK);
            ASSERT_EQ(bl_createBox(engine, nullptr, &mesh), BL_OK);
            bl_MeshProperties properties{};
            ASSERT_EQ(bl_getMeshProperties(mesh, &properties), BL_OK);
            properties.material = material;
            ASSERT_EQ(bl_setMeshProperties(mesh, &properties), BL_OK);
            ASSERT_EQ(bl_meshNode(mesh, &meshNode), BL_OK);
            ASSERT_EQ(bl_addToScene(scene, meshNode), BL_OK);
            const auto status = bl_registerScene(scene);
            if (status != BL_OK)
            {
                bl_Error error{};
                bl_getLastError(runtime->runtime, &error);
                FAIL() << "Native registration: " << status << ' ' << std::string(error.message.data, error.message.length);
            }
        }

        std::array<uint8_t, 4> Center() const
        {
            const auto* value = pixels.data() + (32 * 64 + 32) * 4;
            return {value[0], value[1], value[2], value[3]};
        }
    };
}

TEST_F(LiteNativeGraphics, RuntimeUniformMutationChangesActualGPUReadback)
{
    CreateColoredBox();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    const auto red = Center();
    EXPECT_GT(red[0], 220);
    EXPECT_LT(red[1], 20);
    EXPECT_LT(red[2], 20);
    ASSERT_EQ(bl_setShaderVector3(material, String("tint"), {0, 1, 0}), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    const auto green = Center();
    EXPECT_LT(green[0], 20);
    EXPECT_GT(green[1], 220);
    EXPECT_LT(green[2], 20);
    EXPECT_NE(red, green);
    bl_EngineStats stats{};
    ASSERT_EQ(bl_getEngineStats(engine, &stats), BL_OK);
    EXPECT_EQ(stats.drawCallCount, 1u);
}

TEST_F(LiteNativeGraphics, VisibilityAndLastSceneRemovalDoNotLeaveStaleDraws)
{
    CreateColoredBox();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    ASSERT_GT(Center()[0], 220);
    ASSERT_EQ(bl_setNodeVisible(meshNode, false), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    const auto background = Center();
    EXPECT_NEAR(background[0], 13, 3);
    EXPECT_NEAR(background[1], 26, 3);
    EXPECT_NEAR(background[2], 51, 3);
    bl_EngineStats stats{};
    ASSERT_EQ(bl_getEngineStats(engine, &stats), BL_OK);
    EXPECT_EQ(stats.drawCallCount, 0u);
    ASSERT_EQ(bl_setNodeVisible(meshNode, true), BL_OK);
    ASSERT_EQ(bl_removeFromScene(scene, meshNode), BL_OK);
    EXPECT_EQ(bl_addToScene(scene, meshNode), BL_DISPOSED);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(Center(), background);
}

TEST_F(LiteNativeGraphics, BeforeRenderCallbacksAreLIFOAndMutationsReachTheSameGPUFrame)
{
    CreateColoredBox();
    ASSERT_FALSE(HasFatalFailure());
    struct Callback
    {
        bl_ShaderMaterial material;
        std::vector<int>* order;
        int id;
        bl_Vec3 color;
        bl_Status status{};
        double delta{};
    };
    std::vector<int> order;
    Callback first{material, &order, 1, {1, 0, 0}}, second{material, &order, 2, {0, 1, 0}};
    const auto before = [](void* user, double delta)
    {
        auto& callback = *static_cast<Callback*>(user);
        callback.order->push_back(callback.id);
        callback.delta = delta;
        callback.status = bl_setShaderVector3(callback.material, String("tint"), callback.color);
    };
    bl_CallbackToken firstToken{}, secondToken{};
    ASSERT_EQ(bl_onBeforeRender(scene, before, &first, &firstToken), BL_OK);
    ASSERT_EQ(bl_onBeforeRender(scene, before, &second, &secondToken), BL_OK);
    bl_SceneProperties properties{};
    ASSERT_EQ(bl_getSceneProperties(scene, &properties), BL_OK);
    properties.fixedDeltaMs = 7;
    ASSERT_EQ(bl_setSceneProperties(scene, &properties), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 100), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(order, (std::vector<int>{2, 1}));
    EXPECT_EQ(first.status, BL_OK);
    EXPECT_EQ(second.status, BL_OK);
    EXPECT_EQ(first.delta, 7);
    EXPECT_EQ(second.delta, 7);
    EXPECT_GT(Center()[0], 220);
    EXPECT_LT(Center()[1], 20);
    EXPECT_EQ(bl_removeSceneCallback(scene, firstToken), BL_OK);
    EXPECT_EQ(bl_removeSceneCallback(scene, secondToken), BL_OK);
}

TEST_F(LiteNativeGraphics, BorrowedRangesAreExclusiveAndExternalDeviceCannotBecomeLiteOwned)
{
    bl_NativeEngineOptions native{};
    native.ownership = BL_BGFX_BORROWED;
    native.backend = BL_RENDERER_D3D11;
    native.target = {BL_TARGET_BGFX_FRAMEBUFFER, target.idx, 64, 64, BL_COLOR_RGBA8, BL_DEPTH_D24S8, 1, 8, 8};
    native.hostUserData = this;
    native.isExternalBgfxInitialized = ExternalInitialized;
    native.waitForSubmittedWork = WaitSubmitted;
    bl_EngineContext second{};
    ASSERT_EQ(bl_createEngine(runtime->runtime, &native, nullptr, &second), BL_OK);
    native.target.firstViewId = 4;
    bl_EngineContext overlapping{};
    EXPECT_EQ(bl_createEngine(runtime->runtime, &native, nullptr, &overlapping), BL_BUSY);
    ASSERT_EQ(bl_disposeEngine(engine), BL_OK);
    engine = {};
    native.target.firstViewId = 0;
    bl_EngineContext replacement{};
    ASSERT_EQ(bl_createEngine(runtime->runtime, &native, nullptr, &replacement), BL_OK);
    ASSERT_EQ(bl_disposeEngine(second), BL_OK);
    ASSERT_EQ(bl_disposeEngine(replacement), BL_OK);

    native.ownership = BL_BGFX_OWNED;
    native.target.kind = BL_TARGET_SWAPCHAIN;
    native.target.framebufferIndex = BL_INVALID_BGFX_HANDLE;
    native.platform.nativeWindowHandle = window.window;
    bl_EngineContext forbidden{};
    EXPECT_EQ(bl_createEngine(runtime->runtime, &native, nullptr, &forbidden), BL_BUSY);
    EXPECT_EQ(bgfx::getRendererType(), bgfx::RendererType::Direct3D11);
    EXPECT_TRUE(Readback());
}

TEST_F(LiteNativeGraphics, RemovingOneSceneOwnerKeepsTheMeshLiveUntilTheLastOwnerIsRemoved)
{
    CreateColoredBox();
    ASSERT_FALSE(HasFatalFailure());
    bl_SceneContext secondScene{};
    ASSERT_EQ(bl_createSceneContext(engine, &secondScene), BL_OK);
    bl_SceneProperties properties{};
    ASSERT_EQ(bl_getSceneProperties(scene, &properties), BL_OK);
    ASSERT_EQ(bl_setSceneProperties(secondScene, &properties), BL_OK);
    ASSERT_EQ(bl_addToScene(secondScene, meshNode), BL_OK);
    ASSERT_EQ(bl_registerScene(secondScene), BL_OK);
    ASSERT_EQ(bl_removeFromScene(scene, meshNode), BL_OK);
    bl_MeshProperties meshProperties{};
    EXPECT_EQ(bl_getMeshProperties(mesh, &meshProperties), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_GT(Center()[0], 220);
    ASSERT_EQ(bl_removeFromScene(secondScene, meshNode), BL_OK);
    EXPECT_EQ(bl_getMeshProperties(mesh, &meshProperties), BL_DISPOSED);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_NEAR(Center()[0], 13, 3);
}

TEST(LiteNativeOwnership, OwnedEngineCanBeDisposedAndRecreatedWithoutStaleCapabilityDetection)
{
    WindowHost window;
    window.Start();
    ASSERT_NE(window.window, nullptr);
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    bl_NativeEngineOptions native{};
    native.ownership = BL_BGFX_OWNED;
    native.backend = BL_RENDERER_D3D11;
    native.platform.nativeWindowHandle = window.window;
    native.target = {BL_TARGET_SWAPCHAIN, BL_INVALID_BGFX_HANDLE, 64, 64, BL_COLOR_RGBA8, BL_DEPTH_D24S8, 1, 0, 8};
    native.isExternalBgfxInitialized = [](void*) { return false; };
    for (uint32_t i = 0; i < 2; ++i)
    {
        std::printf("Owned lifecycle %u: host external initialization=false\n", i);
        bl_EngineContext engine{};
        ASSERT_EQ(bl_createEngine(runtime.runtime, &native, nullptr, &engine), BL_OK);
        bl_EngineContext conflicting{};
        EXPECT_EQ(bl_createEngine(runtime.runtime, &native, nullptr, &conflicting), BL_BUSY);
        auto borrowed = native;
        borrowed.ownership = BL_BGFX_BORROWED;
        borrowed.isExternalBgfxInitialized = [](void*) { return true; };
        borrowed.waitForSubmittedWork = [](void*) { return BL_OK; };
        EXPECT_EQ(bl_createEngine(runtime.runtime, &borrowed, nullptr, &conflicting), BL_BUSY);
        ASSERT_EQ(bl_disposeEngine(engine), BL_OK);
        std::printf("Owned lifecycle %u: successful init/dispose\n", i);
    }
}
