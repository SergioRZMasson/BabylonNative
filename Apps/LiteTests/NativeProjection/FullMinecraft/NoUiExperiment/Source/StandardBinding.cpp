#include "StandardBinding.h"
#include "../../Source/Platform.h"
#include <algorithm>
#include <cstring>

namespace
{
    struct MeshObservation
    {
        bl_Mesh mesh;
        bl_SceneNode node;
        bool live{true};
    };

    bool s_validate{};
    NoUi::Observation s_observation;
    std::vector<MeshObservation> s_meshes;
    std::vector<bl_SceneContext> s_scenes;
    std::optional<uint32_t> s_profile;

    template<class T> bool Same(T first, T second)
    {
        return std::memcmp(&first, &second, sizeof(T)) == 0;
    }

    void Bytes(const void* data, size_t bytes)
    {
        if (!s_validate)
        {
            return;
        }
        const auto* source = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < bytes; ++index)
        {
            s_observation.geometryDigest =
                (s_observation.geometryDigest ^ source[index]) * 1099511628211ull;
        }
    }

    template<class T> void Values(const T& values)
    {
        const uint64_t size = values.size();
        Bytes(&size, sizeof(size));
        Bytes(values.data(), values.size() * sizeof(*values.data()));
    }

    void Record(bl_Mesh mesh, size_t vertices, size_t indices)
    {
        if (!s_validate)
        {
            return;
        }
        s_meshes.push_back({mesh, bbl::Node(mesh)});
        ++s_observation.geometryFactories;
        s_observation.vertices += vertices;
        s_observation.indices += indices;
        if (s_profile)
        {
            Bytes(&*s_profile, sizeof(*s_profile));
            s_profile.reset();
        }
    }
}

namespace NoUi
{
    void RequireNoHud()
    {
        if (LiteMinecraft::HudBackingRenders() != 0)
        {
            throw std::runtime_error("No-UI run performed native HUD work.");
        }
    }

    void EnableValidation(bool enabled)
    {
        s_validate = enabled;
    }

    bool ValidationEnabled()
    {
        return s_validate;
    }

    Observation Observe(bbl::Engine& engine)
    {
        auto observation = s_observation;
        observation.sceneCount = s_scenes.size();
        observation.materialCount = engine.state->materialBindings.size();
        observation.liveMeshes = 0;
        for (const auto& mesh : s_meshes)
        {
            if (mesh.live)
            {
                bl_MeshProperties properties{};
                const auto status = bl_getMeshProperties(mesh.mesh, &properties);
                if (status == BL_OK)
                {
                    ++observation.liveMeshes;
                }
                else if (status != BL_DISPOSED)
                {
                    bbl::Check(status);
                }
            }
        }
        if (!s_scenes.empty())
        {
            bl_SceneProperties properties{};
            bbl::Check(bl_getSceneProperties(s_scenes.front(), &properties));
            bbl::Check(
                bl_getNodePosition(bbl::Node(properties.camera), &observation.cameraPosition));
            bbl::Check(bl_getCameraTarget(properties.camera, &observation.cameraTarget));
        }
        return observation;
    }
}

namespace bbl
{
    Engine create_engine(const EngineOptions& options)
    {
        return babylon::createEngine(options);
    }

    Scene create_scene_context(Engine& engine)
    {
        auto scene = babylon::createSceneContext(engine);
        s_scenes.push_back(scene.handle);
        return scene;
    }

    CameraHandle create_free_camera(Engine& engine, Vec3d position, Vec3d target)
    {
        return babylon::createFreeCamera(engine, position, target);
    }

    SceneNodeHandle create_transform_node(Engine& engine, const std::string& name, Vec3d position,
                                          Vec4 quaternion, Vec3 scaling)
    {
        return babylon::createTransformNode(engine, name, position, quaternion, scaling);
    }

    MeshHandle create_box(Engine& engine, BoxOptions options)
    {
        const auto mesh = babylon::createBox(engine, options);
        if (s_validate)
        {
            bl_BoxOptions native{};
            native.width = {true, options.width};
            native.height = {true, options.height};
            native.depth = {true, options.depth};
            bl_GeometryData data{};
            Check(bl_createBoxData(engine.state->runtime, &native, &data));
            Bytes(data.positions, data.vertexCount * 3 * sizeof(float));
            Bytes(data.normals, data.vertexCount * 3 * sizeof(float));
            Bytes(data.indices, data.indexCount * sizeof(uint32_t));
            Bytes(data.uvs, data.vertexCount * 2 * sizeof(float));
            Record(mesh, data.vertexCount, data.indexCount);
            Check(bl_freeGeometryData(engine.state->runtime, &data));
        }
        return mesh;
    }

    MeshData create_sphere_data(SphereOptions options)
    {
        return babylon::createSphereData(options);
    }

    MeshHandle create_retained_mesh_from_data(
        Engine& engine, const std::string& name, const js::F32Array& positions,
        const js::F32Array& normals, const js::U32Array& indices, std::optional<js::F32Array> uvs,
        std::optional<js::F32Array> uvs2, std::optional<js::F32Array> tangents,
        std::optional<js::F32Array> colors)
    {
        const auto mesh = babylon::createMeshFromData(engine, name, positions, normals, indices,
                                                      uvs, uvs2, tangents, colors);
        if (s_validate)
        {
            Bytes(name.data(), name.size());
            Values(positions);
            Values(normals);
            Values(indices);
            for (const auto* stream : {&uvs, &uvs2, &tangents, &colors})
            {
                const bool present = stream->has_value();
                Bytes(&present, sizeof(present));
                if (present)
                {
                    Values(**stream);
                }
            }
            Record(mesh, positions.size() / 3, indices.size());
        }
        return mesh;
    }

    MaterialHandle create_shader_material(Engine& engine, uint32_t variant)
    {
        return babylon::createShaderMaterial(engine, variant);
    }

    StoredTexture create_texture_2d_from_pixels(Engine& engine, const js::U8Array& pixels,
                                                double width, double height,
                                                PixelsTextureOptions options)
    {
        return babylon::createTexture2DFromPixels(engine, pixels, width, height, options);
    }

    void register_scene(Scene& scene)
    {
        babylon::registerScene(scene);
    }

    void on_before_render(Scene& scene, js::Callback<void(double)> callback)
    {
        babylon::onBeforeRender(scene, std::move(callback));
    }

    void set_mesh_visible(Engine&, MeshHandle mesh, bool visible)
    {
        Check(bl_setNodeVisible(Node(mesh), visible));
    }

    void set_shader_pixels_texture(Engine& engine, MaterialHandle material, uint32_t slot,
                                   StoredTexture texture)
    {
        babylon::setShaderTexture(engine, material, slot, texture);
    }

    void RecordMembership(Scene&, bl_SceneNode node, bool added)
    {
        if (!s_validate)
        {
            return;
        }
        if (added)
        {
            ++s_observation.membershipEvents;
        }
        else
        {
            ++s_observation.removals;
            for (auto& mesh : s_meshes)
            {
                if (Same(mesh.node, node))
                {
                    mesh.live = false;
                }
            }
        }
    }

    namespace upstream
    {
        void begin_scene_mesh_profile(Engine&, uint32_t profile)
        {
            if (s_validate)
            {
                s_profile = profile;
            }
        }
    }

    namespace pal
    {
        AudioContextHandle audio_create_context(bl_Runtime*)
        {
            AudioContextHandle context{};
            const auto& service = LiteMinecraft::AudioService();
            Check(service.createContext(service.userData, &context));
            return context;
        }

        AudioNodeHandle audio_destination(AudioContextHandle context)
        {
            auto node = std::make_shared<AudioNodeData>();
            const auto& service = LiteMinecraft::AudioService();
            Check(service.getDestination(service.userData, context, &node->native));
            return node;
        }

        void audio_resume(AudioContextHandle context)
        {
            const auto& service = LiteMinecraft::AudioService();
            Check(service.resumeContext(service.userData, context));
        }
    }
}
