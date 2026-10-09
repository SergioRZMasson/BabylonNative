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
    struct Vec2 { float x; float y; };
    struct Color4 { float r; float g; float b; float a; };
    using MeshHandle = bl_Mesh;
    using MaterialHandle = bl_StandardMaterial;
    using LightHandle = bl_HemisphericLight;
    using CameraHandle = bl_ArcRotateCamera;
    struct EngineOptions { const char* title; int width; int height; };
    struct BoxOptions { float width; float height; float depth; };
    struct GroundOptions { double width; double height; uint32_t subdivisions; Vec2 uvScale; };
    struct Position
    {
        bl_SceneNode node{};
        void operator=(Vec3d value) { PrimitivesHost::Check(bl_setNodePosition(node, value)); }
    };
    struct MeshProjection { bl_Mesh identity{}; Position position; };
    struct MeshStore { std::vector<std::unique_ptr<MeshProjection>> values; };
    struct Enabled
    {
        bl_ArcRotateControl control{};
        void operator=(bool value)
        {
            if (value) throw std::runtime_error("Attach the original control explicitly.");
            PrimitivesHost::Check(bl_detachControl(control));
        }
    };
    struct CameraProjection
    {
        CameraHandle identity{};
        Enabled controls_enabled;
        std::function<bool()> should_handle_pointer_down;
        std::function<bool()> external_drag_active;
        std::function<bool()> external_pick_pending;
        std::function<void()> configurable_free_pointer;
        std::function<void()> configurable_free_update;
    };
    struct CameraStore { std::vector<std::unique_ptr<CameraProjection>> values; };
    struct Engine { bl_EngineContext native{}; MeshStore meshes; CameraStore cameras; };
    struct CameraProperty
    {
        bl_SceneContext scene{};
        void operator=(CameraHandle camera)
        {
            bl_SceneProperties2 properties{};
            PrimitivesHost::Check(bl_getSceneProperties2(scene, &properties));
            PrimitivesHost::Check(bl_arcRotateCameraAsCamera(camera, &properties.camera));
            PrimitivesHost::Check(bl_setSceneProperties2(scene, &properties));
        }
    };
    struct ClearColorProperty
    {
        bl_SceneContext scene{};
        void operator=(Color4 color)
        {
            bl_SceneProperties2 properties{};
            PrimitivesHost::Check(bl_getSceneProperties2(scene, &properties));
            properties.clearColor = {color.r, color.g, color.b, color.a};
            PrimitivesHost::Check(bl_setSceneProperties2(scene, &properties));
        }
    };
    struct Scene { bl_SceneContext native{}; CameraProperty camera; ClearColorProperty clear_color; };
    template<typename Store, typename Handle>
    auto& handle_at(Store& store, Handle handle)
    {
        for (auto& value : store.values)
        {
            if (value->identity._id == handle._id && value->identity._runtime == handle._runtime)
                return *value;
        }
        throw std::runtime_error("Unknown compiler user identity in independent store");
    }
    Engine create_engine(EngineOptions options);
    Scene create_scene_context(Engine& engine);
    CameraHandle create_arc_rotate_camera(Engine&, double, double, double, Vec3d);
    void attach_control(Engine&, CameraHandle, Scene&);
    LightHandle create_hemispheric_light(Engine&, Vec3, double);
    MeshHandle create_box(Engine&, BoxOptions);
    MeshHandle create_ground(Engine&, GroundOptions);
    MaterialHandle create_standard_material(Engine&);
    void set_material_diffuse_color(Engine&, MaterialHandle, const js::Array<double>&);
    void set_mesh_material(Engine&, MeshHandle, MaterialHandle);
    void add_to_scene(Scene&, MeshHandle);
    void add_to_scene(Scene&, LightHandle);
    void register_scene(Scene&);
    void start_engine(Engine&);
    inline void mark_mesh_dirty(Engine&, MeshHandle)
    {
        // Native property writes already invalidate the world transform.
    }
}
