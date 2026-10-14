#include "Binding/bblite/runtime.hpp"

namespace bbl
{
    Engine create_engine(EngineOptions options)
    {
        if (options.width != 1280 || options.height != 720)
            throw std::runtime_error("Canonical viewport differs from native host");
        return {Scene35Host::CreateEngine(), {}, {}};
    }
    Scene create_scene_context(Engine& engine)
    {
        Scene scene{};
        Scene35Host::Check(bl_createSceneContext(engine.native, &scene.native));
        return scene;
    }
    bl_AssetContainer load_gltf(Engine& engine, const std::string& path)
    {
        const bl_AssetSource source{BL_ASSET_SOURCE_URI, {path.data(), path.size()}, {}};
        bl_AssetContainer container{};
        Scene35Host::Check(bl_loadGltf(engine.native, &source, &container));
        return container;
    }
    void add_to_scene(Scene& scene, bl_AssetContainer asset)
    {
        Scene35Host::Check(bl_addAssetContainerToScene(scene.native, asset));
    }
    bl_EnvironmentTextures load_environment(Scene& scene, const EnvironmentOptions& input)
    {
        bl_EnvironmentOptions options{};
        options.brdfUrl = {input.brdf_url.data(), input.brdf_url.size()};
        options.groundTextureUrl = {input.ground_texture_url.data(), input.ground_texture_url.size()};
        options.skyboxUrl = {input.skybox_url.data(), input.skybox_url.size()};
        if (input.skybox_size != 0) options.skyboxSize = {true, input.skybox_size};
        options.skipSkybox = input.skybox_uses_environment || input.solid_skybox ? BL_BOOL_FALSE : BL_BOOL_TRUE;
        options.skipGround = input.ground ? BL_BOOL_FALSE : BL_BOOL_TRUE;
        bl_EnvironmentTextures result{};
        Scene35Host::Check(bl_loadEnvironment(scene.native,
            {input.environment_url.data(), input.environment_url.size()}, &options, &result));
        return result;
    }
    CameraHandle create_default_camera(Engine& engine, Scene& scene)
    {
        CameraHandle camera{};
        Scene35Host::Check(bl_createDefaultCamera(scene.native, &camera));
        bl_ArcRotateCameraProperties properties{};
        Scene35Host::Check(bl_getArcRotateCameraProperties(camera, &properties));
        auto record = std::make_unique<CameraRecord>();
        record->identity = camera;
        record->alpha = properties.alpha;
        record->target = properties.target;
        engine.cameras.push_back(std::move(record));
        return camera;
    }
    void write_camera_scalar(CameraRecord& record, double CameraRecord::* member, double value)
    {
        if (member != &CameraRecord::alpha)
            throw std::runtime_error("Unsupported canonical scalar transport");
        bl_ArcRotateCameraProperties properties{};
        Scene35Host::Check(bl_getArcRotateCameraProperties(record.identity, &properties));
        properties.alpha = value;
        Scene35Host::Check(bl_setArcRotateCameraProperties(record.identity, &properties));
        record.*member = value;
    }
    void attach_control(Engine&, CameraHandle camera, Scene& scene)
    {
        bl_ArcRotateControl control{};
        Scene35Host::Check(bl_attachControl(camera, scene.native, nullptr, &control));
    }
    void register_scene(Scene& scene) { Scene35Host::Check(bl_registerScene(scene.native)); }
    void start_engine(Engine& engine)
    {
        Scene35Host::Check(bl_startEngine(engine.native, nullptr, nullptr));
        for (const auto& callback : engine.continuations) callback();
        Scene35Host::Run(engine.native);
    }
}
