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
    using LightHandle = bl_Light;
    using CameraHandle = bl_ArcRotateCamera;
    struct Color3
    {
        float r;
        float g;
        float b;
    };
    struct EngineOptions
    {
        const char* title;
        int width;
        int height;
    };
    struct CylinderOptions
    {
        double height;
        double diameter_top;
        double diameter_bottom;
        double tessellation;
        double subdivisions;
        bool cone_tip;
    };
    struct PlaneOptions
    {
        float width;
        float height;
    };
    struct DiscOptions
    {
        double radius;
        double tessellation;
        double arc;
    };
    struct PolyhedronOptions
    {
        double size_x;
        double size_y;
        double size_z;
        bool flat;
        std::vector<std::vector<double>> vertices;
        std::vector<std::vector<double>> faces;
    };
    struct RibbonOptions
    {
        std::vector<std::vector<Vec3d>> paths;
        bool close_array;
        bool close_path;
    };
    struct Position
    {
        bl_SceneNode node{};
        void operator=(Vec3d value) { Scene38Host::Check(bl_setNodePosition(node, value)); }
    };
    struct MeshProjection
    {
        MeshHandle identity{};
        Position position;
    };
    struct CameraNumber
    {
        CameraHandle camera{};
        double bl_ArcRotateCameraProperties::* member{};
        void operator=(double value)
        {
            bl_ArcRotateCameraProperties properties{};
            Scene38Host::Check(bl_getArcRotateCameraProperties(camera, &properties));
            properties.*member = value;
            Scene38Host::Check(bl_setArcRotateCameraProperties(camera, &properties));
        }
    };
    struct CameraProjection
    {
        CameraHandle identity{};
        CameraNumber near_plane;
        CameraNumber far_plane;
    };
    struct LightColor
    {
        LightHandle light{};
        bool specular{};
        void operator=(Color3 value);
    };
    struct LightProjection
    {
        LightHandle identity{};
        LightColor diffuse_color;
        LightColor specular_color;
    };
    struct CameraStore
    {
        std::vector<std::unique_ptr<CameraProjection>> values;
    };
    struct MeshStore
    {
        std::vector<std::unique_ptr<MeshProjection>> values;
    };
    struct LightStore
    {
        std::vector<std::unique_ptr<LightProjection>> values;
    };
    struct Engine
    {
        bl_EngineContext native{};
        CameraStore cameras;
        MeshStore meshes;
        LightStore lights;
    };
    struct CameraProperty
    {
        bl_SceneContext scene{};
        void operator=(CameraHandle camera)
        {
            bl_SceneProperties2 properties{};
            Scene38Host::Check(bl_getSceneProperties2(scene, &properties));
            Scene38Host::Check(bl_arcRotateCameraAsCamera(camera, &properties.camera));
            Scene38Host::Check(bl_setSceneProperties2(scene, &properties));
        }
    };
    struct Scene
    {
        bl_SceneContext native{};
        CameraProperty camera;
    };
    template<typename Store, typename Handle>
    auto& handle_at(Store& store, Handle handle)
    {
        for (auto& value : store.values)
        {
            if (value->identity._runtime == handle._runtime && value->identity._id == handle._id)
            {
                return *value;
            }
        }
        throw std::runtime_error("Unknown external scene38 compiler user identity");
    }
    template<typename Record>
    std::vector<Vec3d> vec3_path(const js::Array<js::Ref<Record>>& array)
    {
        std::vector<Vec3d> result;
        result.reserve(array.size());
        for (const auto& point : array)
        {
            result.push_back({point->x, point->y, point->z});
        }
        return result;
    }
    template<typename Record>
    std::vector<std::vector<Vec3d>> vec3_paths(const js::Array<js::Array<js::Ref<Record>>>& array)
    {
        std::vector<std::vector<Vec3d>> result;
        result.reserve(array.size());
        for (const auto& row : array)
        {
            result.push_back(vec3_path(row));
        }
        return result;
    }
    Engine create_engine(EngineOptions);
    Scene create_scene_context(Engine&);
    CameraHandle create_arc_rotate_camera(Engine&, double, double, double, Vec3d);
    LightHandle create_hemispheric_light(Engine&, Vec3, double);
    LightHandle create_directional_light(Engine&, Vec3, double);
    MeshHandle create_cylinder(Engine&, CylinderOptions);
    MeshHandle create_plane(Engine&, PlaneOptions);
    MeshHandle create_disc(Engine&, DiscOptions);
    MeshHandle create_polyhedron(Engine&, const PolyhedronOptions&);
    MeshHandle create_ribbon(Engine&, const RibbonOptions&);
    MeshHandle create_tube(Engine&, const std::vector<Vec3d>&, double, double);
    MeshHandle create_extrude_shape(Engine&, const std::vector<Vec3d>&,
        const std::vector<Vec3d>&, double, double);
    MeshHandle create_tube(Engine&, const std::vector<Vec3d>&, double, double,
        const std::function<double(size_t, double)>&);
    MaterialHandle create_standard_material(Engine&);
    void set_material_diffuse_color(Engine&, MaterialHandle, const js::Array<double>&);
    void set_mesh_material(Engine&, MeshHandle, MaterialHandle);
    void add_to_scene(Scene&, MeshHandle);
    void add_to_scene(Scene&, LightHandle);
    void register_scene(Scene&);
    void start_engine(Engine&);
    inline void mark_mesh_dirty(Engine&, MeshHandle)
    {
        // C99 position transport already invalidates the original lazy world.
    }
}
