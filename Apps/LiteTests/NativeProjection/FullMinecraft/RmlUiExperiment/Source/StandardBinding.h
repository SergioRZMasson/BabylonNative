#pragma once

#include "../../Source/C99Client.h"

namespace MinecraftUi
{
    struct Observation
    {
        uint64_t geometryDigest{1469598103934665603ull};
        size_t geometryFactories{};
        size_t vertices{};
        size_t indices{};
        size_t sceneCount{};
        size_t membershipEvents{};
        size_t removals{};
        size_t liveMeshes{};
        size_t materialCount{};
        bl_Vec3 cameraPosition{};
        bl_Vec3 cameraTarget{};
    };

    void EnableValidation(bool enabled);
    bool ValidationEnabled();
    Observation Observe(bbl::Engine& engine);
}

namespace bbl
{
    struct StandardAudioSourceState;

    struct StandardAudioEngineHandle
    {
        pal::AudioContextHandle context{};
        pal::AudioNodeHandle main_bus;
        js::Set<std::shared_ptr<StandardAudioSourceState>> sources;

        void gc_trace(const js::TraceVisitor& visitor) const
        {
            visitor(main_bus);
            visitor(sources);
        }
    };

    struct StandardAudioSourceState
    {
        pal::AudioNodeHandle source;
        pal::AudioNodeHandle gain;
        StandardAudioEngineHandle engine;

        void gc_trace(const js::TraceVisitor& visitor) const
        {
            visitor(source);
            visitor(gain);
            visitor(engine);
        }
    };

    Engine create_engine(const EngineOptions& options);
    Scene create_scene_context(Engine& engine);
    CameraHandle create_free_camera(Engine& engine, Vec3d position, Vec3d target);
    SceneNodeHandle create_transform_node(Engine& engine, const std::string& name, Vec3d position,
                                          Vec4 quaternion, Vec3 scaling);
    MeshHandle create_box(Engine& engine, BoxOptions options);
    MeshData create_sphere_data(SphereOptions options);
    MeshHandle create_retained_mesh_from_data(
        Engine& engine, const std::string& name, const js::F32Array& positions,
        const js::F32Array& normals, const js::U32Array& indices, std::optional<js::F32Array> uvs,
        std::optional<js::F32Array> uvs2, std::optional<js::F32Array> tangents,
        std::optional<js::F32Array> colors);
    MaterialHandle create_shader_material(Engine& engine, uint32_t variant);
    StoredTexture create_texture_2d_from_pixels(Engine& engine, const js::U8Array& pixels,
                                                double width, double height,
                                                PixelsTextureOptions options);
    void register_scene(Scene& scene);
    void on_before_render(Scene& scene, js::Callback<void(double)> callback);
    void set_mesh_visible(Engine& engine, MeshHandle mesh, bool visible);
    void set_shader_pixels_texture(Engine& engine, MaterialHandle material, uint32_t slot,
                                   StoredTexture texture);

    template<class... T>
    void set_scene_shader_uniform_value(Engine& engine, uint32_t variant, uint32_t offset,
                                        T... values)
    {
        babylon::setSceneShaderUniform(engine, variant, offset, values...);
    }

    void RecordMembership(Scene& scene, bl_SceneNode node, bool added);

    template<class T> void standard_add_to_scene(Scene& scene, T handle)
    {
        const auto node = Node(handle);
        Check(bl_addToScene(scene.handle, node));
        RecordMembership(scene, node, true);
    }

    template<class T> void remove_from_scene(Scene& scene, T handle)
    {
        const auto node = Node(handle);
        Check(bl_removeFromScene(scene.handle, node));
        RecordMembership(scene, node, false);
    }

    namespace upstream
    {
        void begin_scene_mesh_profile(Engine& engine, uint32_t profile);
    }

    namespace pal
    {
        AudioContextHandle audio_create_context(bl_Runtime* runtime);
        AudioNodeHandle audio_destination(AudioContextHandle context);
        void audio_resume(AudioContextHandle context);
    }
}

// The canonical standard-mode program names these compiler data records.
// Native-only facade records keep their distinct names and ABI unchanged.
#define AudioEngineHandle StandardAudioEngineHandle
#define AudioSourceState StandardAudioSourceState
#define add_to_scene standard_add_to_scene
