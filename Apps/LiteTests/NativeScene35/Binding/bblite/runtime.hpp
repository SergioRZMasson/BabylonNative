#pragma once
#include "../../Host.h"
#include <bblite/js_data.hpp>
#include <bblite/js_gc.hpp>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace bbl
{
    using CameraHandle = bl_ArcRotateCamera;
    struct EngineOptions { const char* title; int width; int height; };
    struct CameraRecord
    {
        CameraHandle identity{};
        double alpha{};
        bl_Vec3 target{};
        bool controls_enabled{true};
        std::function<bool()> should_handle_pointer_down;
        std::function<bool()> external_drag_active;
        std::function<bool()> external_pick_pending;
        std::function<void()> configurable_free_pointer;
        std::function<void()> configurable_free_update;
    };
    struct Engine
    {
        bl_EngineContext native{};
        std::vector<std::unique_ptr<CameraRecord>> cameras;
        std::vector<std::function<void()>> continuations;
    };
    struct Scene { bl_SceneContext native{}; };
    struct EnvironmentOptions
    {
        std::string environment_url;
        std::string ground_texture_url;
        std::string skybox_url;
        double skybox_size;
        std::string brdf_url;
        bool skybox_uses_environment;
        bool solid_skybox;
        bool ground;
    };
    inline CameraRecord& handle_at(std::vector<std::unique_ptr<CameraRecord>>& store, CameraHandle h)
    {
        for (auto& record : store)
            if (record->identity._runtime == h._runtime && record->identity._id == h._id)
                return *record;
        throw std::runtime_error("Unissued canonical user camera identity");
    }
    Engine create_engine(EngineOptions options);
    Scene create_scene_context(Engine& engine);
    bl_AssetContainer load_gltf(Engine& engine, const std::string& path);
    void add_to_scene(Scene& scene, bl_AssetContainer asset);
    bl_EnvironmentTextures load_environment(Scene& scene, const EnvironmentOptions& options);
    CameraHandle create_default_camera(Engine& engine, Scene& scene);
    void write_camera_scalar(CameraRecord& record, double CameraRecord::* member, double value);
    void attach_control(Engine& engine, CameraHandle camera, Scene& scene);
    void register_scene(Scene& scene);
    void start_engine(Engine& engine);
    inline std::string asset_path(const std::string& name) { return Scene35Host::AssetPath(name); }
    inline void defer_start_continuation(Engine& engine, std::function<void()> callback)
    {
        engine.continuations.push_back(std::move(callback));
    }
}
