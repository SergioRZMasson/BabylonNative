#pragma once
#include "../../Host.h"
#include <bblite/js_data.hpp>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace bbl
{
    using Vec3 = bl_Vec3;
    using Vec3d = bl_Vec3;
    using MeshHandle = bl_Mesh;
    using MaterialHandle = bl_StandardMaterial;
    using LightHandle = bl_DirectionalLight;
    using CameraHandle = bl_ArcRotateCamera;
    struct Color3 { float r; float g; float b; };
    struct EngineOptions { const char* title; int width; int height; };
    struct SphereOptions { uint32_t segments; double diameter_x; double diameter_y; double diameter_z; };
    struct Enabled
    {
        bl_ArcRotateControl control{};
        void operator=(bool value)
        {
            if (value) throw std::runtime_error("Attach the original control explicitly.");
            Scene2Host::Check(bl_detachControl(control));
        }
    };
    struct CameraNumber
    {
        CameraHandle camera{};
        double bl_ArcRotateCameraProperties::* member{};
        void operator=(double value)
        {
            bl_ArcRotateCameraProperties properties{};
            Scene2Host::Check(bl_getArcRotateCameraProperties(camera, &properties));
            properties.*member = value;
            Scene2Host::Check(bl_setArcRotateCameraProperties(camera, &properties));
        }
    };
    struct CameraProjection
    {
        CameraHandle identity{};
        CameraNumber near_plane;
        CameraNumber far_plane;
        Enabled controls_enabled;
        std::function<bool()> should_handle_pointer_down;
        std::function<bool()> external_drag_active;
        std::function<bool()> external_pick_pending;
        std::function<void()> configurable_free_pointer;
        std::function<void()> configurable_free_update;
    };
    struct LightColor
    {
        LightHandle light{};
        bool specular{};
        void operator=(Color3 value)
        {
            bl_DirectionalLightProperties properties{};
            Scene2Host::Check(bl_getDirectionalLightProperties(light, &properties));
            (specular ? properties.specular : properties.diffuse) = {value.r, value.g, value.b};
            Scene2Host::Check(bl_setDirectionalLightProperties(light, &properties));
        }
    };
    struct LightProjection
    {
        LightHandle identity{};
        LightColor diffuse_color;
        LightColor specular_color;
    };
    struct MeshProjection { MeshHandle identity{}; };
    struct CameraStore
    {
        using HandleType = CameraHandle;
        std::vector<std::unique_ptr<CameraProjection>> values;
    };
    struct LightStore
    {
        using HandleType = LightHandle;
        std::vector<std::unique_ptr<LightProjection>> values;
    };
    struct MeshStore { std::vector<std::unique_ptr<MeshProjection>> values; };
    struct Engine { bl_EngineContext native{}; CameraStore cameras; LightStore lights; MeshStore meshes; };
    struct CameraProperty
    {
        bl_SceneContext scene{};
        CameraHandle identity{};
        void operator=(CameraHandle camera)
        {
            bl_SceneProperties2 properties{};
            Scene2Host::Check(bl_getSceneProperties2(scene, &properties));
            Scene2Host::Check(bl_arcRotateCameraAsCamera(camera, &properties.camera));
            Scene2Host::Check(bl_setSceneProperties2(scene, &properties));
            identity = camera;
        }
        operator CameraHandle() const { return identity; }
    };
    struct Scene { bl_SceneContext native{}; CameraProperty camera; };
    template<typename Store, typename Handle>
    auto& handle_at(Store& store, Handle handle)
    {
        const typename Store::HandleType identity = handle;
        for (auto& value : store.values)
        {
            if (value->identity._runtime == identity._runtime && value->identity._id == identity._id)
                return *value;
        }
        throw std::runtime_error("Unknown external scene2 user identity");
    }
    Engine create_engine(EngineOptions);
    Scene create_scene_context(Engine&);
    CameraHandle create_arc_rotate_camera(Engine&, double, double, double, Vec3d);
    void attach_control(Engine&, CameraHandle, Scene&);
    LightHandle create_directional_light(Engine&, Vec3, double);
    MeshHandle create_sphere(Engine&, SphereOptions);
    MaterialHandle create_standard_material(Engine&);
    void set_mesh_material(Engine&, MeshHandle, MaterialHandle);
    void add_to_scene(Scene&, MeshHandle);
    void add_to_scene(Scene&, LightHandle);
    void register_scene(Scene&);
    void start_engine(Engine&);
}
