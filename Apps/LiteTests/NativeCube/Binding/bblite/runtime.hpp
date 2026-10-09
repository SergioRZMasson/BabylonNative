#pragma once
#include "../../Host.h"
#include <bblite/js_data.hpp>
#include <memory>
#include <stdexcept>

namespace bbl
{
    using Vec3 = bl_Vec3;
    using Vec3d = bl_Vec3;
    using MeshHandle = bl_Mesh;
    using MaterialHandle = bl_StandardMaterial;
    using LightHandle = bl_HemisphericLight;
    using CameraHandle = bl_ArcRotateCamera;
    struct EngineOptions { const char* title; int width; int height; };
    struct BoxOptions { float width; float height; float depth; };
    struct Component
    {
        bl_SceneNode node{};
        void operator+=(float delta)
        {
            bl_Vec3 value{};
            CubeHost::Check(bl_getNodeRotation(node, &value));
            value.y = static_cast<float>(static_cast<float>(value.y) + delta);
            CubeHost::Check(bl_setNodeRotation(node, value));
        }
    };
    struct MeshProjection { struct Rotation { Component y; } rotation; };
    struct MeshStore
    {
        MeshProjection value;
        bl_Mesh identity{};
    };
    struct Engine
    {
        bl_EngineContext native{};
        MeshStore meshes;
    };
    struct CameraProperty
    {
        bl_SceneContext scene{};
        void operator=(CameraHandle camera)
        {
            bl_SceneProperties2 properties{};
            CubeHost::Check(bl_getSceneProperties2(scene, &properties));
            CubeHost::Check(bl_arcRotateCameraAsCamera(camera, &properties.camera));
            CubeHost::Check(bl_setSceneProperties2(scene, &properties));
        }
    };
    struct Scene { bl_SceneContext native{}; CameraProperty camera; };
    inline MeshProjection& handle_at(MeshStore& store, MeshHandle handle)
    {
        if (handle._id != store.identity._id || handle._runtime != store.identity._runtime)
            throw std::runtime_error("Unknown compiler user mesh identity");
        return store.value;
    }
    Engine create_engine(EngineOptions options);
    Scene create_scene_context(Engine& engine);
    CameraHandle create_arc_rotate_camera(Engine&, double alpha, double beta, double radius, Vec3d target);
    LightHandle create_hemispheric_light(Engine&, Vec3 direction, double intensity);
    MeshHandle create_box(Engine& engine, BoxOptions options);
    MaterialHandle create_standard_material(Engine&);
    void set_material_diffuse_color(Engine&, MaterialHandle material, const js::Array<double>& color);
    void set_mesh_material(Engine&, MeshHandle mesh, MaterialHandle material);
    void add_to_scene(Scene& scene, MeshHandle mesh);
    void add_to_scene(Scene& scene, LightHandle light);
    void register_scene(Scene& scene);
    void start_engine(Engine& engine);
    inline void mark_mesh_dirty(Engine&, MeshHandle)
    {
        // C99 property writes already perform push dirty invalidation.
    }
    template<typename Callback>
    void on_before_render(Scene& scene, Callback callback)
    {
        CubeHost::RetainCallback([callback](double delta) mutable {
            callback(static_cast<float>(delta));
        }, scene.native);
    }
}
