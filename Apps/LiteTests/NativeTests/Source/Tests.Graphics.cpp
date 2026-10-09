#include "TestRuntime.h"
#include "LiteInternal.h"
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

TEST_F(LiteNativeGraphics, OriginalArcHemisphericStandardUsesRuntimeWGSLAndRealPixels)
{
    bl_ArcRotateCamera camera{};
    ASSERT_EQ(bl_createArcRotateCamera(runtime->runtime, -3.141592653589793 / 2, 1.1, 5, {}, &camera), BL_OK);
    bl_Camera family{};
    ASSERT_EQ(bl_arcRotateCameraAsCamera(camera, &family), BL_OK);
    bl_SceneProperties2 sceneProperties{};
    ASSERT_EQ(bl_getSceneProperties2(scene, &sceneProperties), BL_OK);
    sceneProperties.camera = family;
    ASSERT_EQ(bl_setSceneProperties2(scene, &sceneProperties), BL_OK);
    bl_SceneProperties sentinel{};
    sentinel.fixedDeltaMs = 123;
    EXPECT_EQ(bl_getSceneProperties(scene, &sentinel), BL_UNSUPPORTED);
    EXPECT_EQ(sentinel.fixedDeltaMs, 123);
    bl_HemisphericLight light{};
    ASSERT_EQ(bl_createHemisphericLight(runtime->runtime, nullptr, &light), BL_OK);
    bl_Light lightFamily{};
    ASSERT_EQ(bl_hemisphericLightAsLight(light, &lightFamily), BL_OK);
    bl_SceneNode lightNode{};
    ASSERT_EQ(bl_lightNode(lightFamily, &lightNode), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, lightNode), BL_OK);
    bl_StandardMaterial standard{};
    ASSERT_EQ(bl_createStandardMaterial(runtime->runtime, &standard), BL_OK);
    bl_StandardMaterialProperties properties{};
    ASSERT_EQ(bl_getStandardMaterialProperties(standard, &properties), BL_OK);
    properties.diffuseColor = {.85, .34, .2};
    ASSERT_EQ(bl_setStandardMaterialProperties(standard, &properties), BL_OK);
    bl_Material materialFamily{};
    ASSERT_EQ(bl_standardMaterialAsMaterial(standard, &materialFamily), BL_OK);
    ASSERT_EQ(bl_createBox(engine, nullptr, &mesh), BL_OK);
    bl_MeshProperties2 meshProperties{materialFamily, {}, false};
    ASSERT_EQ(bl_setMeshProperties2(mesh, &meshProperties), BL_OK);
    ASSERT_EQ(bl_meshNode(mesh, &meshNode), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, meshNode), BL_OK);
    const auto registered = bl_registerScene(scene);
    bl_Error error{};
    bl_getLastError(runtime->runtime, &error);
    ASSERT_EQ(registered, BL_OK) << std::string(error.message.data ? error.message.data : "", error.message.length);
    ASSERT_EQ(bl_renderFrame(engine, 1000.0 / 60), BL_OK);
    ASSERT_TRUE(Readback());
    size_t upward = 0;
    size_t vertical = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        if (std::abs(int(pixels[i]) - 217) <= 1 &&
            std::abs(int(pixels[i + 1]) - 87) <= 1 &&
            std::abs(int(pixels[i + 2]) - 51) <= 1)
        {
            ++upward;
        }
        if (std::abs(int(pixels[i]) - 108) <= 1 &&
            std::abs(int(pixels[i + 1]) - 43) <= 1 &&
            std::abs(int(pixels[i + 2]) - 26) <= 1)
        {
            ++vertical;
        }
    }
    EXPECT_GT(upward, 5u);
    EXPECT_GT(vertical, 30u);
    auto* nativeScene = reinterpret_cast<L_Scene*>(l_peek(runtime->runtime, scene._id));
    ASSERT_NE(nativeScene->standardPackets[1].pipeline, nullptr);
    EXPECT_EQ(nativeScene->standardPackets[1].pipeline->blockCount, 4u);
    EXPECT_GT(nativeScene->standardPackets[1].pipeline->nativeUniformCount, 1u);
    const auto before = Center();
    ASSERT_EQ(bl_setLightIntensity(lightFamily, 0), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_LT(Center()[0], 3);
    ASSERT_EQ(bl_setLightIntensity(lightFamily, 1), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(Center(), before);
    bl_HemisphericLightProperties lightProperties{};
    ASSERT_EQ(bl_getHemisphericLightProperties(light, &lightProperties), BL_OK);
    lightProperties.diffuseColor = {};
    ASSERT_EQ(bl_setHemisphericLightProperties(light, &lightProperties), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(Center(), before);
    ASSERT_EQ(bl_markLightUboDirty(lightFamily), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_LT(Center()[0], before[0] / 2);
    lightProperties.diffuseColor = {1, 1, 1};
    ASSERT_EQ(bl_setHemisphericLightProperties(light, &lightProperties), BL_OK);
    ASSERT_EQ(bl_markLightUboDirty(lightFamily), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(Center(), before);
    properties.diffuseColor = {0, 1, 0};
    ASSERT_EQ(bl_setStandardMaterialProperties(standard, &properties), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(Center(), before);
    ASSERT_EQ(bl_markMaterialUboDirty(materialFamily), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    const auto green = Center();
    EXPECT_GT(green[1], green[0] + 50);
    EXPECT_GT(green[1], green[2] + 50);
    bl_RebuildMaterialOptions unsupported{};
    unsupported.rebuildFrameGraph = BL_BOOL_TRUE;
    properties.disableLighting = true;
    properties.emissiveColor = {1, 1, 1};
    ASSERT_EQ(bl_setStandardMaterialProperties(standard, &properties), BL_OK);
    auto* previous = nativeScene->standardPackets[1].pipeline;
    EXPECT_EQ(bl_rebuildMaterial(scene, materialFamily, &unsupported), BL_UNSUPPORTED);
    EXPECT_EQ(nativeScene->standardPackets[1].pipeline, previous);
    ASSERT_EQ(bl_rebuildMaterial(scene, materialFamily, nullptr), BL_OK);
    EXPECT_NE(nativeScene->standardPackets[1].pipeline, previous);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_GT(Center()[1], 240);
    bl_GeometryData box{};
    ASSERT_EQ(bl_createBoxData(runtime->runtime, nullptr, &box), BL_OK);
    std::vector<float> colors(box.vertexCount * 4, 1);
    bl_MeshGeometry colored{{box.positions, box.vertexCount * 3},
        {box.normals, box.vertexCount * 3}, {box.indices, box.indexCount},
        {box.uvs, box.vertexCount * 2}, {}, {}, {colors.data(), colors.size()}};
    ASSERT_EQ(bl_resizeMeshGeometry(engine, mesh, &colored), BL_OK);
    EXPECT_EQ(bl_renderFrame(engine, 16), BL_UNSUPPORTED);
    colored.colors = {};
    ASSERT_EQ(bl_resizeMeshGeometry(engine, mesh, &colored), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_EQ(bl_freeGeometryData(runtime->runtime, &box), BL_OK);
}

TEST_F(LiteNativeGraphics, ArcControlAppendsOriginalPerFrameInertiaAfterUserPrepend)
{
    bl_ArcRotateCamera camera{};
    ASSERT_EQ(bl_createArcRotateCamera(runtime->runtime, 0, 1, 5, {}, &camera), BL_OK);
    bl_Camera family{};
    ASSERT_EQ(bl_arcRotateCameraAsCamera(camera, &family), BL_OK);
    bl_SceneProperties2 properties{};
    ASSERT_EQ(bl_getSceneProperties2(scene, &properties), BL_OK);
    properties.camera = family;
    ASSERT_EQ(bl_setSceneProperties2(scene, &properties), BL_OK);
    bl_ArcRotateControl control{};
    ASSERT_EQ(bl_attachControl(camera, scene, nullptr, &control), BL_OK);
    struct Observer
    {
        bl_ArcRotateCamera camera;
        bl_ArcRotateControl control;
        double alpha{};
        bl_Status status{};
        bl_Status replacement{};
    } observer{camera, control};
    bl_CallbackToken callback{};
    ASSERT_EQ(bl_onBeforeRender(scene, [](void* user, double) {
        auto* observer = static_cast<Observer*>(user);
        bl_ArcRotateCameraProperties p{};
        observer->status = bl_getArcRotateCameraProperties(observer->camera, &p);
        observer->alpha = p.alpha;
        bl_ArcRotateControlOptions options{};
        options.primaryButton = BL_ARC_ACTION_PAN;
        observer->replacement = bl_setArcRotateControlOptions(observer->control, &options);
    }, &observer, &callback), BL_OK);
    bl_ArcRotateInput input{};
    bl_ArcRotateInputEffects effects{};
    input.kind = BL_ARC_POINTER_DOWN;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    input.kind = BL_ARC_POINTER_MOVE;
    input.clientX = 100;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    ASSERT_EQ(bl_registerScene(scene), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 3), BL_OK);
    EXPECT_EQ(observer.status, BL_OK);
    EXPECT_EQ(observer.replacement, BL_OK);
    EXPECT_DOUBLE_EQ(observer.alpha, 0);
    bl_ArcRotateCameraProperties p{};
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_DOUBLE_EQ(p.alpha, -.1);
    ASSERT_EQ(bl_renderFrame(engine, 333), BL_OK);
    EXPECT_DOUBLE_EQ(observer.alpha, -.1);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_NEAR(p.alpha, -.19, 1e-15);
    ASSERT_EQ(bl_disposeScene(scene), BL_OK);
    input.kind = BL_ARC_WHEEL;
    input.deltaY = 120;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_DOUBLE_EQ(p.radius, 5);
    EXPECT_DOUBLE_EQ(p.inertialRadiusOffset, -.2);
    ASSERT_EQ(bl_detachControl(control), BL_OK);
}

TEST_F(LiteNativeGraphics, StandardPerMeshOrderAndSharedOwnerVersionsAreIndependent)
{
    bl_StandardMaterial standard{};
    ASSERT_EQ(bl_createStandardMaterial(runtime->runtime, &standard), BL_OK);
    bl_StandardMaterialProperties p{};
    ASSERT_EQ(bl_getStandardMaterialProperties(standard, &p), BL_OK);
    p.disableLighting = true;
    p.emissiveColor = {1, 1, 1};
    ASSERT_EQ(bl_setStandardMaterialProperties(standard, &p), BL_OK);
    bl_Material family{};
    ASSERT_EQ(bl_standardMaterialAsMaterial(standard, &family), BL_OK);
    bl_Mesh first{}, second{};
    ASSERT_EQ(bl_createBox(engine, nullptr, &first), BL_OK);
    ASSERT_EQ(bl_createBox(engine, nullptr, &second), BL_OK);
    bl_MeshProperties2 properties{family, {true, 200}, false};
    ASSERT_EQ(bl_setMeshProperties2(first, &properties), BL_OK);
    properties.renderOrder.value = 10;
    ASSERT_EQ(bl_setMeshProperties2(second, &properties), BL_OK);
    bl_SceneNode a{}, b{};
    ASSERT_EQ(bl_meshNode(first, &a), BL_OK);
    ASSERT_EQ(bl_meshNode(second, &b), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, a), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, b), BL_OK);
    bl_SceneContext other{};
    ASSERT_EQ(bl_createSceneContext(engine, &other), BL_OK);
    bl_SceneProperties2 sceneProperties{};
    ASSERT_EQ(bl_getSceneProperties2(scene, &sceneProperties), BL_OK);
    ASSERT_EQ(bl_setSceneProperties2(other, &sceneProperties), BL_OK);
    ASSERT_EQ(bl_addToScene(other, a), BL_OK);
    ASSERT_EQ(bl_registerScene(scene), BL_OK);
    ASSERT_EQ(bl_registerScene(other), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    auto* s = reinterpret_cast<L_Scene*>(l_peek(runtime->runtime, scene._id));
    auto* t = reinterpret_cast<L_Scene*>(l_peek(runtime->runtime, other._id));
    const auto* draws = static_cast<L_Draw*>(s->drawScratch);
    EXPECT_EQ(draws[0].mesh->node.record.id, b._id);
    EXPECT_EQ(draws[0].order, 10);
    EXPECT_EQ(draws[1].order, 200);
    p.diffuseColor = {0, 1, 0};
    ASSERT_EQ(bl_setStandardMaterialProperties(standard, &p), BL_OK);
    ASSERT_EQ(bl_markMaterialUboDirty(family), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    EXPECT_EQ(s->standardPackets[0].properties.diffuseColor.y, 1);
    EXPECT_EQ(t->standardPackets[0].properties.diffuseColor.y, 1);
    p.disableLighting = false;
    p.backFaceCulling = false;
    p.alpha = .5;
    ASSERT_EQ(bl_setStandardMaterialProperties(standard, &p), BL_OK);
    ASSERT_EQ(bl_rebuildMaterial(scene, family, nullptr), BL_OK);
    EXPECT_TRUE(s->standardPackets[0].transparent);
    EXPECT_FALSE(s->standardPackets[0].culling);
    EXPECT_FALSE(t->standardPackets[0].transparent);
    EXPECT_TRUE(t->standardPackets[0].culling);
    ASSERT_EQ(bl_removeFromScene(scene, a), BL_OK);
    bl_Vec3 rotation{};
    EXPECT_EQ(bl_getNodeRotation(a, &rotation), BL_OK);
    EXPECT_EQ(bl_disposeStandardMaterial(standard), BL_BUSY);
    ASSERT_EQ(bl_removeFromScene(other, a), BL_OK);
    EXPECT_EQ(bl_getNodeRotation(a, &rotation), BL_DISPOSED);
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

TEST_F(LiteNativeGraphics, HiddenMinimumOrderAndCallbackSwapsReachCurrentDraws)
{
    CreateColoredBox();
    ASSERT_FALSE(HasFatalFailure());
    bl_ShaderMaterial green{};
    const bl_VertexSemantic attributes[]{BL_ATTRIBUTE_POSITION};
    const double tint[]{0, 1, 0};
    const bl_ShaderUniformDecl uniforms[]{
        {String("worldViewProjection"), BL_UNIFORM_MAT4, {}, true},
        {String("tint"), BL_UNIFORM_VEC3, {tint, 3}, false}};
    bl_ShaderMaterialOptions options{};
    options.vertexSource = String(Vertex);
    options.fragmentSource = String(Fragment);
    options.attributes = attributes;
    options.attributeCount = 1;
    options.uniforms = uniforms;
    options.uniformCount = 2;
    ASSERT_EQ(bl_createShaderMaterial(runtime->runtime, &options, &green), BL_OK);
    bl_Mesh second{}, hidden{};
    ASSERT_EQ(bl_createBox(engine, nullptr, &second), BL_OK);
    ASSERT_EQ(bl_createBox(engine, nullptr, &hidden), BL_OK);
    bl_MeshProperties properties{};
    properties.material = green;
    properties.renderOrder = {true, 0};
    ASSERT_EQ(bl_setMeshProperties(second, &properties), BL_OK);
    properties.material = material;
    properties.renderOrder = {true, -10};
    ASSERT_EQ(bl_setMeshProperties(hidden, &properties), BL_OK);
    bl_SceneNode secondNode{}, hiddenNode{};
    ASSERT_EQ(bl_meshNode(second, &secondNode), BL_OK);
    ASSERT_EQ(bl_meshNode(hidden, &hiddenNode), BL_OK);
    ASSERT_EQ(bl_setNodeVisible(hiddenNode, false), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, secondNode), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, hiddenNode), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, secondNode), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_GT(Center()[1], 220);
    bl_EngineStats stats{};
    ASSERT_EQ(bl_getEngineStats(engine, &stats), BL_OK);
    EXPECT_EQ(stats.drawCallCount, 3u);
    struct Mutation
    {
        bl_Mesh hidden;
        bl_ShaderMaterial green;
        bl_Status status{};
    } mutation{hidden, green};
    bl_CallbackToken token{};
    ASSERT_EQ(bl_onBeforeRender(scene, [](void* user, double)
    {
        auto& state = *static_cast<Mutation*>(user);
        bl_MeshProperties current{};
        state.status = bl_getMeshProperties(state.hidden, &current);
        if (state.status == BL_OK)
        {
            current.material = state.green;
            state.status = bl_setMeshProperties(state.hidden, &current);
        }
    }, &mutation, &token), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(mutation.status, BL_OK);
    EXPECT_GT(Center()[0], 220);
    ASSERT_EQ(bl_removeSceneCallback(scene, token), BL_OK);
    ASSERT_EQ(bl_removeFromScene(scene, hiddenNode), BL_OK);
    EXPECT_EQ(bl_addToScene(scene, hiddenNode), BL_DISPOSED);
    ASSERT_EQ(bl_removeFromScene(scene, secondNode), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    ASSERT_EQ(bl_getEngineStats(engine, &stats), BL_OK);
    EXPECT_EQ(stats.drawCallCount, 1u);
    EXPECT_GT(Center()[0], 220);
}

TEST_F(LiteNativeGraphics, GeometryFailuresPreserveCPUAndGPUIdentityAndPartialUpdatesStayGPUOnly)
{
    CreateColoredBox();
    ASSERT_FALSE(HasFatalFailure());
    L_Record* record{};
    ASSERT_EQ(l_get(runtime->runtime, mesh._id, L_MESH, &record), BL_OK);
    auto* native = reinterpret_cast<L_Mesh*>(record);
    bl_GeometryData data{};
    ASSERT_EQ(bl_createBoxData(runtime->runtime, nullptr, &data), BL_OK);
    bl_MeshGeometry geometry{{data.positions, data.vertexCount * 3},
        {data.normals, data.vertexCount * 3}, {data.indices, data.indexCount},
        {data.uvs, data.vertexCount * 2}, {}, {}, {}};
    const auto vertexBuffer = native->geometry.vertexBuffer.idx;
    const auto indexBuffer = native->geometry.indexBuffer.idx;
    const auto position = native->geometry.streams[0][0];
    const auto minimum = native->geometry.minimum;
    data.positions[0] += .25f;
    const size_t one = 1;
    ASSERT_EQ(bl_updateMeshPositions(engine, mesh, {data.positions, data.vertexCount * 3},
        0, &one, 0), BL_OK);
    EXPECT_EQ(native->geometry.streams[0][0], position);
    EXPECT_EQ(native->geometry.minimum.x, minimum.x);
    float uploaded{};
    std::memcpy(&uploaded, native->geometry.gpuVertices, sizeof(uploaded));
    EXPECT_EQ(uploaded, data.positions[0]);
    struct Failure
    {
        int after;
        bl_Allocator original;
    } failure{-1, runtime->runtime->allocator};
    auto& allocator = runtime->runtime->allocator;
    allocator.userData = &failure;
    allocator.allocate = [](void* user, size_t bytes, size_t alignment) -> void*
    {
        auto& state = *static_cast<Failure*>(user);
        if (state.after >= 0 && state.after-- == 0) return nullptr;
        return state.original.allocate(state.original.userData, bytes, alignment);
    };
    allocator.deallocate = [](void* user, void* memory, size_t bytes, size_t alignment)
    {
        auto& state = *static_cast<Failure*>(user);
        state.original.deallocate(state.original.userData, memory, bytes, alignment);
    };
    for (int index = 0; index < 5; ++index)
    {
        failure.after = index;
        EXPECT_EQ(bl_updateMeshGeometry(engine, mesh, &geometry), BL_OUT_OF_MEMORY);
        EXPECT_EQ(native->geometry.vertexBuffer.idx, vertexBuffer);
        EXPECT_EQ(native->geometry.indexBuffer.idx, indexBuffer);
        EXPECT_EQ(native->geometry.streams[0][0], position);
        std::memcpy(&uploaded, native->geometry.gpuVertices, sizeof(uploaded));
        EXPECT_EQ(uploaded, data.positions[0]);
    }
    failure.after = -1;
    ASSERT_EQ(bl_updateMeshGeometry(engine, mesh, &geometry), BL_OK);
    EXPECT_EQ(native->geometry.streams[0][0], data.positions[0]);
    EXPECT_EQ(native->geometry.vertexBuffer.idx, vertexBuffer);
    EXPECT_EQ(native->geometry.indexBuffer.idx, indexBuffer);
    allocator = failure.original;
    ASSERT_EQ(bl_freeGeometryData(runtime->runtime, &data), BL_OK);
}

TEST_F(LiteNativeGraphics, GroundFactoryCopiesIndependentDataAndPreservesUpdateContracts)
{
    bl_GroundOptions options{};
    options.width = {true, 8};
    options.height = {true, 8};
    bl_Mesh first{};
    bl_Mesh second{};
    ASSERT_EQ(bl_createGround(engine, &options, &first), BL_OK);
    ASSERT_EQ(bl_createGround(engine, &options, &second), BL_OK);
    auto* a = reinterpret_cast<L_Mesh*>(l_peek(runtime->runtime, first._id));
    auto* b = reinterpret_cast<L_Mesh*>(l_peek(runtime->runtime, second._id));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->geometry.vertices, 4u);
    EXPECT_EQ(a->geometry.indexCount, 6u);
    EXPECT_NE(a->geometry.streams[0], b->geometry.streams[0]);
    EXPECT_NE(a->geometry.vertexBuffer.idx, b->geometry.vertexBuffer.idx);
    EXPECT_EQ(a->geometry.minimum.x, -4);
    EXPECT_EQ(a->geometry.maximum.z, 4);
    bl_SceneNode node{};
    ASSERT_EQ(bl_meshNode(first, &node), BL_OK);
    bl_String name{};
    ASSERT_EQ(bl_getNodeName(node, &name), BL_OK);
    EXPECT_EQ(std::string(name.data, name.length), "ground");
    bl_MeshProperties2 properties{};
    ASSERT_EQ(bl_getMeshProperties2(first, &properties), BL_OK);
    EXPECT_EQ(properties.material._id, 0u);
    EXPECT_FALSE(properties.receiveShadows);
    bl_GeometryData data{};
    ASSERT_EQ(bl_createFlatGroundData(runtime->runtime, &options, &data), BL_OK);
    data.positions[0] = -8;
    EXPECT_EQ(a->geometry.streams[0][0], -4);
    const auto vertexBuffer = a->geometry.vertexBuffer.idx;
    ASSERT_EQ(bl_updateMeshPositions(engine, first, {data.positions, 12}, 0, nullptr, 0), BL_OK);
    EXPECT_EQ(a->geometry.streams[0][0], -4);
    EXPECT_EQ(a->geometry.minimum.x, -4);
    bl_MeshGeometry geometry{{data.positions, 12}, {data.normals, 12}, {data.indices, 6},
                            {data.uvs, 8}, {}, {}, {}};
    ASSERT_EQ(bl_updateMeshGeometry(engine, first, &geometry), BL_OK);
    EXPECT_EQ(a->geometry.minimum.x, -8);
    EXPECT_EQ(a->geometry.vertexBuffer.idx, vertexBuffer);
    EXPECT_EQ(b->geometry.minimum.x, -4);
    ASSERT_EQ(bl_freeGeometryData(runtime->runtime, &data), BL_OK);
    bl_Mesh sentinel{nullptr, 123};
    options.subdivisions = {true, 20000};
    EXPECT_EQ(bl_createGround(engine, &options, &sentinel), BL_INVALID_ARGUMENT);
    EXPECT_EQ(sentinel._id, 123u);
    EXPECT_EQ(bl_createGround(engine, nullptr, nullptr), BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_createGround({first._runtime, first._id}, nullptr, &sentinel), BL_INVALID_HANDLE);
    ASSERT_EQ(bl_disposeNode(node), BL_OK);
    EXPECT_EQ(bl_createGround({engine._runtime, first._id}, nullptr, &sentinel), BL_INVALID_HANDLE);
}

TEST_F(LiteNativeGraphics, GroundFactoryAllocationFailuresNeverPublishPartialMeshOrData)
{
    struct Failure
    {
        bl_Allocator original;
        size_t calls{};
        size_t fail{};
    };
    const auto live = [&] {
        size_t count{};
        for (size_t i = 0; i < runtime->runtime->count; ++i)
        {
            const auto* record = runtime->runtime->records[i];
            count += record && !record->disposed;
        }
        return count;
    };
    for (size_t fail = 1; fail <= 24; ++fail)
    {
        Failure state{runtime->runtime->allocator, 0, fail};
        const size_t before = live();
        runtime->runtime->allocator = {&state,
            [](void* user, size_t bytes, size_t alignment) -> void* {
                auto& value = *static_cast<Failure*>(user);
                if (++value.calls == value.fail)
                {
                    return nullptr;
                }
                return value.original.allocate(value.original.userData, bytes, alignment);
            },
            [](void* user, void* memory, size_t bytes, size_t alignment) {
                auto& value = *static_cast<Failure*>(user);
                value.original.deallocate(value.original.userData, memory, bytes, alignment);
            }};
        bl_Mesh candidate{nullptr, 123};
        const auto status = bl_createGround(engine, nullptr, &candidate);
        runtime->runtime->allocator = state.original;
        if (state.calls >= fail)
        {
            EXPECT_EQ(status, BL_OUT_OF_MEMORY);
            EXPECT_EQ(candidate._id, 123u);
            EXPECT_EQ(candidate._runtime, nullptr);
            EXPECT_EQ(live(), before);
        }
        else
        {
            ASSERT_EQ(status, BL_OK);
            bl_SceneNode node{};
            ASSERT_EQ(bl_meshNode(candidate, &node), BL_OK);
            ASSERT_EQ(bl_disposeNode(node), BL_OK);
            EXPECT_EQ(live(), before);
        }
    }
}

TEST_F(LiteNativeGraphics, CompilerPartialFailureReleasesExactlyOnceAndCanRetry)
{
    struct CompilerCalls
    {
        bl_ShaderCompilerService original;
        size_t compiles{};
        size_t releases{};
        bool fail{true};
    } calls{runtime->runtime->compiler};
    runtime->runtime->compiler.userData = &calls;
    runtime->runtime->compiler.compile = [](void* user, const bl_ShaderCompileRequest* request,
        bl_ShaderCompileResult* result)
    {
        auto& state = *static_cast<CompilerCalls*>(user);
        ++state.compiles;
        const auto status = state.original.compile(state.original.userData, request, result);
        return state.fail && status == BL_OK ? BL_SHADER_ERROR : status;
    };
    runtime->runtime->compiler.release = [](void* user, bl_ShaderCompileResult* result)
    {
        auto& state = *static_cast<CompilerCalls*>(user);
        ++state.releases;
        state.original.release(state.original.userData, result);
    };
    const bl_VertexSemantic attributes[]{BL_ATTRIBUTE_POSITION};
    const bl_ShaderUniformDecl uniforms[]{
        {String("worldViewProjection"), BL_UNIFORM_MAT4, {}, true},
        {String("tint"), BL_UNIFORM_VEC3, {}, false}};
    bl_ShaderMaterialOptions options{};
    options.vertexSource = String(Vertex);
    options.fragmentSource = String(Fragment);
    options.attributes = attributes;
    options.attributeCount = 1;
    options.uniforms = uniforms;
    options.uniformCount = 2;
    ASSERT_EQ(bl_createShaderMaterial(runtime->runtime, &options, &material), BL_OK);
    ASSERT_EQ(bl_createBox(engine, nullptr, &mesh), BL_OK);
    bl_MeshProperties properties{};
    properties.material = material;
    ASSERT_EQ(bl_setMeshProperties(mesh, &properties), BL_OK);
    ASSERT_EQ(bl_meshNode(mesh, &meshNode), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, meshNode), BL_OK);
    EXPECT_EQ(bl_registerScene(scene), BL_SHADER_ERROR);
    EXPECT_EQ(calls.compiles, 1u);
    EXPECT_EQ(calls.releases, 1u);
    calls.fail = false;
    ASSERT_EQ(bl_registerScene(scene), BL_OK);
    EXPECT_EQ(calls.compiles, 2u);
    EXPECT_EQ(calls.releases, 2u);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    EXPECT_EQ(calls.compiles, 2u);
    runtime->runtime->compiler = calls.original;
}

TEST_F(LiteNativeGraphics, ExplicitEmptyRangesStillClearRetiredIndexElements)
{
    CreateColoredBox();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    ASSERT_GT(Center()[0], 220);
    bl_GeometryData data{};
    ASSERT_EQ(bl_createBoxData(runtime->runtime, nullptr, &data), BL_OK);
    bl_MeshGeometry geometry{{data.positions, data.vertexCount * 3},
        {data.normals, data.vertexCount * 3}, {}, {data.uvs, data.vertexCount * 2}, {}, {}, {}};
    bl_GeometryUpdateRanges ranges{};
    bl_GeometryCapacityResult capacity{};
    ASSERT_EQ(bl_updateMeshGeometryCapacity(engine, mesh, &geometry, nullptr, &ranges,
        &capacity), BL_OK);
    ASSERT_TRUE(capacity.stable);
    std::vector<uint32_t> zeros(data.indexCount);
    geometry.indices = {zeros.data(), zeros.size()};
    ASSERT_EQ(bl_updateMeshGeometryCapacity(engine, mesh, &geometry, nullptr, &ranges,
        &capacity), BL_OK);
    ASSERT_TRUE(capacity.stable);
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_NEAR(Center()[0], 13, 3);
    EXPECT_NEAR(Center()[1], 26, 3);
    ASSERT_EQ(bl_freeGeometryData(runtime->runtime, &data), BL_OK);
}

TEST_F(LiteNativeGraphics, InvalidReflectionStringsFailBeforeDereferenceAndReleaseTheResult)
{
    struct MalformedCompiler
    {
        bl_ShaderCompilerService original;
        bl_ShaderCompileResult owned{};
        std::vector<bl_ReflectedAttribute> attributes;
        std::vector<bl_ReflectedUniform> uniforms;
        size_t releases{};
        unsigned mode{};
    } compilerState{runtime->runtime->compiler};
    runtime->runtime->compiler.userData = &compilerState;
    runtime->runtime->compiler.compile = [](void* user, const bl_ShaderCompileRequest* request,
        bl_ShaderCompileResult* result)
    {
        auto& state = *static_cast<MalformedCompiler*>(user);
        const auto status = state.original.compile(state.original.userData, request, result);
        state.owned = *result;
        if (status == BL_OK)
        {
            state.attributes.assign(result->attributes, result->attributes + result->attributeCount);
            state.uniforms.assign(result->uniforms, result->uniforms + result->uniformCount);
            if (state.mode == 0)
            {
                state.attributes.at(0).name.data = nullptr;
            }
            else if (state.mode == 1)
            {
                state.uniforms.at(0).name.data = nullptr;
            }
            else if (state.mode == 2)
            {
                state.uniforms.at(0).nativeName.data = nullptr;
            }
            else if (state.mode == 3)
            {
                state.uniforms.at(0).nativeByteOffset = UINT32_MAX;
            }
            else if (state.mode == 4)
            {
                state.uniforms.at(0).byteOffset = UINT32_MAX;
            }
            else if (state.mode == 5 || state.mode == 6)
            {
                auto& first = state.uniforms.at(0);
                auto& second = state.uniforms.at(1);
                second.nativeName = first.nativeName;
                second.nativeType = first.nativeType;
                second.nativeCount = static_cast<uint16_t>(
                    first.nativeCount + (state.mode == 6 ? 1 : 0));
                second.nativeByteOffset = first.nativeByteOffset;
            }
            else if (state.mode == 7)
            {
                state.uniforms.at(0).stages = 0;
            }
            result->attributes = state.attributes.data();
            result->uniforms = state.uniforms.data();
        }
        return status;
    };
    runtime->runtime->compiler.release = [](void* user, bl_ShaderCompileResult* result)
    {
        auto& state = *static_cast<MalformedCompiler*>(user);
        ++state.releases;
        state.original.release(state.original.userData, &state.owned);
        *result = {};
    };
    const bl_VertexSemantic attribute = BL_ATTRIBUTE_POSITION;
    const bl_ShaderUniformDecl uniforms[]{
        {String("worldViewProjection"), BL_UNIFORM_MAT4, {}, true},
        {String("tint"), BL_UNIFORM_VEC3, {}, false}};
    bl_ShaderMaterialOptions options{};
    options.attributes = &attribute;
    options.attributeCount = 1;
    options.uniforms = uniforms;
    options.uniformCount = 2;
    options.vertexSource = String(Vertex);
    options.fragmentSource = String(Fragment);
    ASSERT_EQ(bl_createShaderMaterial(runtime->runtime, &options, &material), BL_OK);
    ASSERT_EQ(bl_createBox(engine, nullptr, &mesh), BL_OK);
    bl_MeshProperties properties{};
    properties.material = material;
    ASSERT_EQ(bl_setMeshProperties(mesh, &properties), BL_OK);
    ASSERT_EQ(bl_meshNode(mesh, &meshNode), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, meshNode), BL_OK);
    for (unsigned mode = 0; mode < 8; ++mode)
    {
        compilerState.mode = mode;
        EXPECT_EQ(bl_registerScene(scene), BL_SHADER_ERROR) << "Corruption mode " << mode;
        EXPECT_EQ(compilerState.releases, mode + 1);
        bl_Error error{};
        ASSERT_EQ(bl_getLastError(runtime->runtime, &error), BL_OK);
        EXPECT_GT(error.message.length, 0u);
    }
    compilerState.mode = 8;
    EXPECT_EQ(bl_registerScene(scene), BL_OK);
    EXPECT_EQ(compilerState.releases, 9u);
    runtime->runtime->compiler = compilerState.original;
    ASSERT_EQ(bl_renderFrame(engine, 16), BL_OK);
    ASSERT_TRUE(Readback());
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
