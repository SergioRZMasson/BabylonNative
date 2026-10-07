#include "C99Client.h"
#include <cstring>
#include <iostream>

namespace bbl
{
    namespace
    {
        std::vector<std::weak_ptr<EngineState>> s_engines;
    }

    bl_String String(const char* value)
    {
        return {value, std::strlen(value)};
    }

    bl_String String(const std::string& value)
    {
        return {value.data(), value.size()};
    }

    void Check(bl_Status status)
    {
        if (status != BL_OK)
        {
            bl_Error error{};
            bl_getLastError(LiteMinecraft::Runtime(), &error);
            throw std::runtime_error(
                "C99 status " + std::to_string(status) + ": " +
                std::string(error.message.data ? error.message.data : "", error.message.length));
        }
    }

    bl_SceneNode Node(bl_Mesh mesh)
    {
        bl_SceneNode node{};
        Check(bl_meshNode(mesh, &node));
        return node;
    }

    bl_SceneNode Node(bl_FreeCamera camera)
    {
        bl_SceneNode node{};
        Check(bl_cameraNode(camera, &node));
        return node;
    }

    bl_SceneNode Node(bl_SceneNode node)
    {
        return node;
    }

    namespace
    {
        bl_Vec3 ReadVector(bl_SceneNode node, VectorProperty property, bl_FreeCamera camera = {})
        {
            bl_Vec3 value{};
            switch (property)
            {
                case VectorProperty::Position:
                    Check(bl_getNodePosition(node, &value));
                    break;
                case VectorProperty::Scaling:
                    Check(bl_getNodeScaling(node, &value));
                    break;
                case VectorProperty::Rotation:
                    Check(bl_getNodeRotation(node, &value));
                    break;
                case VectorProperty::CameraTarget:
                    Check(bl_getCameraTarget(camera, &value));
                    break;
            }
            return value;
        }

        void WriteVector(bl_SceneNode node, VectorProperty property, bl_Vec3 value,
                         bl_FreeCamera camera = {})
        {
            switch (property)
            {
                case VectorProperty::Position:
                    Check(bl_setNodePosition(node, value));
                    break;
                case VectorProperty::Scaling:
                    Check(bl_setNodeScaling(node, value));
                    break;
                case VectorProperty::Rotation:
                    Check(bl_setNodeRotation(node, value));
                    break;
                case VectorProperty::CameraTarget:
                    Check(bl_setCameraTarget(camera, value));
                    break;
            }
        }

        double& Component(bl_Vec3& value, uint32_t index)
        {
            if (index == 0)
            {
                return value.x;
            }
            if (index == 1)
            {
                return value.y;
            }
            if (index == 2)
            {
                return value.z;
            }
            throw std::runtime_error("Invalid vector component.");
        }
    }

    ScalarProperty::operator double() const
    {
        auto value = ReadVector(node, property, camera);
        return Component(value, component);
    }

    double ScalarProperty::operator=(double number)
    {
        auto value = ReadVector(node, property, camera);
        Component(value, component) = number;
        WriteVector(node, property, value, camera);
        return number;
    }

    double ScalarProperty::operator=(const ScalarProperty& value)
    {
        return operator=(static_cast<double>(value));
    }

    double ScalarProperty::operator+=(double value)
    {
        return operator=(static_cast<double>(*this) + value);
    }

    VectorProxy::VectorProxy(bl_SceneNode node, VectorProperty property, bl_FreeCamera camera)
        : x{node, property, 0, camera}
        , y{node, property, 1, camera}
        , z{node, property, 2, camera}
    {
    }

    Vec3 VectorProxy::operator=(Vec3 value)
    {
        WriteVector(x.node, x.property, value, x.camera);
        return value;
    }

    double MeshOrder::operator=(double value)
    {
        bl_MeshProperties properties{};
        Check(bl_getMeshProperties(mesh, &properties));
        properties.renderOrder = {true, value};
        Check(bl_setMeshProperties(mesh, &properties));
        return value;
    }

    bool MeshOrderPresent::operator=(bool value)
    {
        bl_MeshProperties properties{};
        Check(bl_getMeshProperties(mesh, &properties));
        properties.renderOrder.present = value;
        Check(bl_setMeshProperties(mesh, &properties));
        return value;
    }

    MeshRecord::MeshRecord(bl_Mesh mesh)
        : position{Node(mesh), VectorProperty::Position}
        , scaling{Node(mesh), VectorProperty::Scaling}
        , rotation{Node(mesh), VectorProperty::Rotation}
        , render_order{mesh}
        , has_render_order{mesh}
    {
    }

    double CameraPlane::operator=(double value)
    {
        bl_CameraProperties properties{};
        Check(bl_getCameraProperties(camera, &properties));
        if (near)
        {
            properties.nearPlane = value;
        }
        else
        {
            properties.farPlane = value;
        }
        Check(bl_setCameraProperties(camera, &properties));
        return value;
    }

    CameraRecord::CameraRecord(bl_FreeCamera camera)
        : handle{camera}
        , position{Node(camera), VectorProperty::Position}
        , target{Node(camera), VectorProperty::CameraTarget, camera}
        , near_plane{camera, true}
        , far_plane{camera, false}
    {
    }

    MeshRecord handle_at(MeshCollection, MeshHandle mesh)
    {
        return MeshRecord(mesh);
    }

    CameraRecord handle_at(CameraCollection, CameraHandle camera)
    {
        return CameraRecord(camera);
    }

    TransformRecord handle_at(TransformCollection, SceneNodeHandle node)
    {
        bl_String name{};
        Check(bl_getNodeName(node, &name));
        return {std::string(name.data, name.length)};
    }

    void write_camera_vector_component(CameraRecord camera, VectorProxy CameraRecord::* property,
                                       double Vec3d::* component, double number)
    {
        auto& proxy = camera.*property;
        auto value = ReadVector(proxy.x.node, proxy.x.property, proxy.x.camera);
        value.*component = number;
        WriteVector(proxy.x.node, proxy.x.property, value, proxy.x.camera);
    }

    EngineState::~EngineState()
    {
        beforeRender.clear();
        if (engineCreated && LiteMinecraft::Runtime() == runtime)
        {
            const auto status = bl_disposeEngine(engine);
            if (status != BL_OK)
            {
                std::cerr << "Native engine teardown status: " << status << "\n";
            }
        }
    }

    Color4 SceneColor::operator=(Color4 value)
    {
        bl_SceneProperties properties{};
        Check(bl_getSceneProperties(scene, &properties));
        properties.clearColor = value;
        Check(bl_setSceneProperties(scene, &properties));
        return value;
    }

    CameraHandle SceneCamera::operator=(CameraHandle value)
    {
        bl_SceneProperties properties{};
        Check(bl_getSceneProperties(scene, &properties));
        properties.camera = value;
        Check(bl_setSceneProperties(scene, &properties));
        return value;
    }

    namespace babylon
    {
        Engine createEngine(const EngineOptions&)
        {
            Engine engine{};
            engine.state = std::make_shared<EngineState>();
            engine.state->runtime = LiteMinecraft::Runtime();
            const auto native = LiteMinecraft::NativeOptions();
            Check(bl_createEngine(engine.state->runtime, &native, nullptr, &engine.state->engine));
            engine.state->engineCreated = true;
            engine.audio_session = engine.state->runtime;
            engine.pointer_locked.state = engine.state;
            s_engines.push_back(engine.state);
            return engine;
        }

        Scene createSceneContext(Engine& engine)
        {
            bl_SceneContext handle{};
            Check(bl_createSceneContext(engine.state->engine, &handle));
            return {handle, engine.state, {handle}, {handle}};
        }

        CameraHandle createFreeCamera(Engine& engine, Vec3d position, Vec3d target)
        {
            CameraHandle camera{};
            Check(bl_createFreeCamera(engine.state->runtime, position, target, &camera));
            return camera;
        }

        SceneNodeHandle createTransformNode(Engine& engine, const std::string& name, Vec3d position,
                                            Vec4 quaternion, Vec3 scaling)
        {
            SceneNodeHandle node{};
            const bl_TransformNodeOptions options{position, quaternion, scaling};
            Check(bl_createTransformNode(engine.state->runtime, String(name), &options, &node));
            return node;
        }

        MeshHandle createBox(Engine& engine, BoxOptions options)
        {
            bl_BoxOptions native{};
            native.width = {true, options.width};
            native.height = {true, options.height};
            native.depth = {true, options.depth};
            MeshHandle mesh{};
            Check(bl_createBox(engine.state->engine, &native, &mesh));
            return mesh;
        }

        MeshData createSphereData(SphereOptions options)
        {
            bl_SphereOptions native{};
            native.segments = {true, static_cast<double>(options.segments)};
            native.diameterX = {true, options.diameter_x};
            native.diameterY = {true, options.diameter_y};
            native.diameterZ = {true, options.diameter_z};
            bl_GeometryData data{};
            Check(bl_createSphereData(LiteMinecraft::Runtime(), &native, &data));
            MeshData result{{data.positions, data.positions + data.vertexCount * 3},
                            {data.normals, data.normals + data.vertexCount * 3},
                            {data.uvs, data.uvs + data.vertexCount * 2},
                            {data.indices, data.indices + data.indexCount},
                            static_cast<uint32_t>(data.vertexCount),
                            static_cast<uint32_t>(data.indexCount)};
            Check(bl_freeGeometryData(LiteMinecraft::Runtime(), &data));
            return result;
        }

        MeshHandle createMeshFromData(Engine& engine, const std::string& name,
                                      const js::F32Array& positions, const js::F32Array& normals,
                                      const js::U32Array& indices, std::optional<js::F32Array> uvs,
                                      std::optional<js::F32Array> uvs2,
                                      std::optional<js::F32Array> tangents,
                                      std::optional<js::F32Array> colors)
        {
            const auto span = [](const std::optional<js::F32Array>& values) -> bl_F32Span
            {
                if (values)
                {
                    return {values->data(), values->size()};
                }
                return {};
            };
            const bl_MeshGeometry geometry{{positions.data(), positions.size()},
                                           {normals.data(), normals.size()},
                                           {indices.data(), indices.size()},
                                           span(uvs),
                                           span(uvs2),
                                           span(tangents),
                                           span(colors)};
            MeshHandle mesh{};
            Check(bl_createMeshFromData(engine.state->engine, String(name), &geometry, &mesh));
            return mesh;
        }

        MaterialHandle createShaderMaterial(Engine& engine, uint32_t variant)
        {
            MaterialHandle material{};
            Check(bl_createShaderMaterial(engine.state->runtime,
                                          &MaterialDescription(variant).options, &material.native));
            material.variant = variant;
            return material;
        }

        StoredTexture createTexture2DFromPixels(Engine& engine, const js::U8Array& pixels,
                                                double width, double height,
                                                PixelsTextureOptions options)
        {
            bl_PixelsTexture2DOptions native{};
            native.addressModeU = static_cast<bl_AddressMode>(options.address_u);
            native.addressModeV = static_cast<bl_AddressMode>(options.address_v);
            native.minFilter = static_cast<bl_FilterMode>(options.min_filter);
            native.magFilter = static_cast<bl_FilterMode>(options.mag_filter);
            native.srgb = options.srgb;
            StoredTexture texture{};
            Check(bl_createTexture2DFromPixels(engine.state->engine, {pixels.data(), pixels.size()},
                                               static_cast<uint32_t>(width),
                                               static_cast<uint32_t>(height), &native, &texture));
            return texture;
        }

        void registerScene(Scene& scene)
        {
            Check(bl_registerScene(scene.handle));
        }

        void onBeforeRender(Scene& scene, js::Callback<void(double)> callback)
        {
            scene.state->beforeRender.insert(scene.state->beforeRender.begin(),
                                             std::move(callback));
            if (scene.state->beforeRender.size() != 1)
            {
                return;
            }
            bl_CallbackToken token{};
            Check(bl_onBeforeRender(
                scene.handle,
                [](void* userData, double deltaMs)
                {
                    auto& state = *static_cast<EngineState*>(userData);
                    try
                    {
                        for (auto& item : state.beforeRender)
                        {
                            item(deltaMs);
                        }
                    }
                    catch (...)
                    {
                        state.callbackError = std::current_exception();
                    }
                },
                scene.state.get(), &token));
            scene.state->callbacks.emplace_back(scene.handle, token);
        }

        void setSubtreeVisible(Engine&, SceneNodeHandle node, bool visible)
        {
            Check(bl_setSubtreeVisible(node, visible));
        }

        void setShaderTexture(Engine&, MaterialHandle material, uint32_t slot,
                              StoredTexture texture)
        {
            const auto& options = MaterialDescription(material.variant).options;
            if (slot >= options.samplerCount)
            {
                throw std::runtime_error("Unknown material sampler slot.");
            }
            Check(bl_setShaderTexture(material.native, options.samplers[slot].name, texture));
        }

        void SetSceneUniform(Engine& engine, uint32_t variant, uint32_t offset, bl_F32Span values)
        {
            SetUniform(engine, engine.state->materialBindings.at(variant), offset, values);
        }

        AudioEngineHandle createAudioEngineAsync(bl_Runtime* runtime)
        {
            AudioEngineHandle engine{};
            Check(bl_createAudioEngineAsync(runtime, nullptr, &engine.native));
            const bl_AudioService* service{};
            Check(bl_getAudioContext(engine.native, &engine.context, &service));
            LiteMinecraft::RegisterAudioEngine(engine.native);
            return engine;
        }

        void unlockAudioEngineAsync(AudioEngineHandle engine)
        {
            Check(bl_unlockAudioEngineAsync(engine.native));
        }

        bl_AudioInputSource createSoundSourceAsync(AudioEngineHandle engine,
                                                   pal::AudioNodeHandle node)
        {
            bl_AudioInputSource source{};
            Check(bl_createSoundSourceAsync(engine.native, node->native, nullptr, &source));
            return source;
        }
    }

    MaterialHandle remember_scene_material(Engine& engine, uint32_t variant,
                                           MaterialHandle material)
    {
        if (engine.state->materialBindings.contains(variant))
        {
            throw std::runtime_error(
                "This application projection refuses repeated instances "
                "of a factory whose emitted scene-uniform setters lost material identity.");
        }
        engine.state->materialBindings[variant] = material;
        return material;
    }

    void SetUniform(Engine&, MaterialHandle material, uint32_t offset, bl_F32Span values)
    {
        const auto& description = MaterialDescription(material.variant);
        for (size_t index = 0; index < description.slotCount; ++index)
        {
            const auto& slot = description.slots[index];
            if (slot.offset == offset && slot.count == values.count)
            {
                Check(bl_setShaderUniformF32(material.native, slot.name, values));
                return;
            }
        }
        throw std::runtime_error("Unknown original uniform offset/count.");
    }

    void set_mesh_material(Engine&, MeshHandle mesh, MaterialHandle material)
    {
        bl_MeshProperties properties{};
        Check(bl_getMeshProperties(mesh, &properties));
        properties.material = material.native;
        Check(bl_setMeshProperties(mesh, &properties));
    }

    void mark_mesh_dirty(Engine&, MeshHandle mesh)
    {
        bl_MeshProperties properties{};
        Check(bl_getMeshProperties(mesh, &properties));
    }

    void set_mesh_transform_parent(Engine&, MeshHandle mesh, SceneNodeHandle parent)
    {
        Check(bl_setNodeParent(Node(mesh), parent));
    }

    void push_transform_node_child(Engine&, SceneNodeHandle parent, MeshHandle mesh)
    {
        Check(bl_appendNodeChild(parent, Node(mesh)));
    }

    void set_scene_node_position_component(Engine&, SceneNodeHandle node, uint32_t component,
                                           double value)
    {
        ScalarProperty proxy{node, VectorProperty::Position, component};
        proxy = value;
    }

    void set_scene_node_rotation(Engine&, SceneNodeHandle node, Vec3 value)
    {
        Check(bl_setNodeRotation(node, value));
    }

    std::string asset_path(const std::string& name)
    {
        return (std::filesystem::path(LITE_MINECRAFT_ASSETS) / name).string();
    }

    void start_engine(Engine& engine)
    {
        LiteMinecraft::Run(engine);
    }

}

namespace LiteMinecraft
{
    void ReleaseEngines()
    {
        for (const auto& weak : bbl::s_engines)
        {
            if (const auto state = weak.lock())
            {
                for (const auto& callback : state->callbacks)
                {
                    bbl::Check(bl_removeSceneCallback(callback.first, callback.second));
                }
                state->callbacks.clear();
                state->beforeRender.clear();
                if (state->engineCreated)
                {
                    bbl::Check(bl_disposeEngine(state->engine));
                    state->engineCreated = false;
                }
            }
        }
        bbl::s_engines.clear();
    }
}
