#pragma once

#include <babylon_lite.h>
#include <bblite/js_data.hpp>
#include <bblite/js_json.hpp>
#include <bblite/js_synchronous_promise.hpp>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bbl
{
    using Vec3 = bl_Vec3;
    using Vec3d = bl_Vec3;
    using Vec4 = bl_Vec4;
    using Color4 = bl_Color4;
    using MeshHandle = bl_Mesh;
    using CameraHandle = bl_FreeCamera;
    using SceneNodeHandle = bl_SceneNode;

    struct MaterialHandle
    {
        bl_ShaderMaterial native;
        uint32_t variant;
    };

    using StoredTexture = bl_Texture2D;
    using UiElementHandle = uint32_t;

    bl_String String(const char* value);
    bl_String String(const std::string& value);
    void Check(bl_Status status);
    bl_SceneNode Node(bl_Mesh mesh);
    bl_SceneNode Node(bl_FreeCamera camera);
    bl_SceneNode Node(bl_SceneNode node);

    enum class VectorProperty
    {
        Position,
        Scaling,
        Rotation,
        CameraTarget
    };

    struct ScalarProperty
    {
        bl_SceneNode node{};
        VectorProperty property{};
        uint32_t component{};
        bl_FreeCamera camera{};
        operator double() const;
        double operator=(double value);
        double operator=(const ScalarProperty& value);
        double operator+=(double value);
    };

    struct VectorProxy
    {
        ScalarProperty x;
        ScalarProperty y;
        ScalarProperty z;
        VectorProxy(bl_SceneNode node, VectorProperty property, bl_FreeCamera camera = {});
        Vec3 operator=(Vec3 value);
    };

    struct MeshOrder
    {
        bl_Mesh mesh;
        double operator=(double value);
    };

    struct MeshOrderPresent
    {
        bl_Mesh mesh;
        bool operator=(bool value);
    };

    struct MeshRecord
    {
        VectorProxy position;
        VectorProxy scaling;
        VectorProxy rotation;
        MeshOrder render_order;
        MeshOrderPresent has_render_order;
        explicit MeshRecord(bl_Mesh mesh);
    };

    struct CameraPlane
    {
        bl_FreeCamera camera;
        bool near;
        double operator=(double value);
    };

    struct CameraRecord
    {
        bl_FreeCamera handle;
        VectorProxy position;
        VectorProxy target;
        CameraPlane near_plane;
        CameraPlane far_plane;
        explicit CameraRecord(bl_FreeCamera camera);
    };

    struct TransformRecord
    {
        std::string name;
    };

    struct MeshCollection
    {
    };

    struct CameraCollection
    {
    };

    struct TransformCollection
    {
    };

    MeshRecord handle_at(MeshCollection, MeshHandle mesh);
    CameraRecord handle_at(CameraCollection, CameraHandle camera);
    TransformRecord handle_at(TransformCollection, SceneNodeHandle node);
    void write_camera_vector_component(CameraRecord camera, VectorProxy CameraRecord::* property,
                                       double Vec3d::* component, double value);

    struct UniformSlot
    {
        uint32_t offset;
        bl_String name;
        uint32_t count;
    };

    struct MaterialMetadata
    {
        bl_ShaderMaterialOptions options;
        const UniformSlot* slots;
        size_t slotCount;
    };

    const MaterialMetadata& MaterialDescription(uint32_t variant);

    struct EngineState
    {
        bl_Runtime* runtime{};
        bl_EngineContext engine{};
        std::map<uint32_t, MaterialHandle> materialBindings;
        std::exception_ptr callbackError;
        std::vector<js::Callback<void(double)>> beforeRender;
        std::vector<std::pair<bl_SceneContext, bl_CallbackToken>> callbacks;
        bool pointerLocked{};
        bool engineCreated{};
        ~EngineState();
    };

    struct PointerLockProperty
    {
        std::weak_ptr<EngineState> state;

        operator bool() const
        {
            return state.lock()->pointerLocked;
        }
    };

    struct Engine
    {
        std::shared_ptr<EngineState> state;
        MeshCollection meshes;
        CameraCollection cameras;
        TransformCollection transform_nodes;
        bl_Runtime* audio_session{};
        PointerLockProperty pointer_locked;
    };

    struct SceneColor
    {
        bl_SceneContext scene;
        Color4 operator=(Color4 value);
    };

    struct SceneCamera
    {
        bl_SceneContext scene;
        CameraHandle operator=(CameraHandle value);
    };

    struct Scene
    {
        bl_SceneContext handle;
        std::shared_ptr<EngineState> state;
        SceneColor clear_color;
        SceneCamera camera;
    };

    struct EngineOptions
    {
        std::string title;
        uint32_t width;
        uint32_t height;
    };

    struct BoxOptions
    {
        double width;
        double height;
        double depth;
    };

    struct SphereOptions
    {
        uint32_t segments;
        double diameter_x;
        double diameter_y;
        double diameter_z;
    };

    struct MeshData
    {
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> uvs;
        std::vector<uint32_t> indices;
        uint32_t vertex_count;
        uint32_t index_count;
    };

    enum class TextureFilter
    {
        nearest,
        linear
    };
    enum class TextureAddressMode
    {
        clamp,
        repeat,
        mirror
    };

    struct PixelsTextureOptions
    {
        TextureFilter min_filter;
        bool has_min_filter;
        TextureFilter mag_filter;
        bool has_mag_filter;
        TextureAddressMode address_u;
        bool has_address_u;
        TextureAddressMode address_v;
        bool has_address_v;
        bool srgb;
    };

    namespace pal
    {
        using AudioContextHandle = bl_HostAudioContext;

        struct AudioNodeData
        {
            bl_HostAudioNode native{};
            ~AudioNodeData();
        };

        using AudioNodeHandle = std::shared_ptr<AudioNodeData>;

        struct AudioBufferData
        {
            bl_HostAudioBuffer native{};
            js::F32Array samples;
            ~AudioBufferData();
        };

        using AudioBufferHandle = std::shared_ptr<AudioBufferData>;
        enum class AudioParamName
        {
            Gain,
            Frequency,
            PlaybackRate
        };
        enum class BiquadFilterKind
        {
            Lowpass
        };
        enum class OscillatorWave
        {
            Sine,
            Triangle
        };

        struct AudioParamHandle
        {
            AudioNodeHandle node;
            bl_AudioParameter parameter;
        };

        std::vector<uint8_t> read_binary_file(const std::string& path);
        double audio_sample_rate(AudioContextHandle context);
        double audio_current_time(AudioContextHandle context);
        AudioNodeHandle audio_create_gain(AudioContextHandle context);
        AudioNodeHandle audio_create_oscillator(AudioContextHandle context);
        AudioNodeHandle audio_create_biquad_filter(AudioContextHandle context);
        AudioNodeHandle audio_create_buffer_source(AudioContextHandle context);
        AudioBufferHandle audio_create_buffer(AudioContextHandle context, uint32_t channels,
                                              uint32_t frames, double sampleRate);
        js::F32Array audio_buffer_channel(AudioBufferHandle buffer, uint32_t channel);
        AudioParamHandle audio_node_param(AudioNodeHandle node, AudioParamName parameter);
        void audio_param_set_value(AudioParamHandle parameter, double value);
        void audio_param_set_value_at_time(AudioParamHandle parameter, double value, double time);
        void audio_param_exponential_ramp(AudioParamHandle parameter, double value, double time);
        void audio_connect(AudioNodeHandle source, AudioNodeHandle target);
        void audio_connect_param(AudioNodeHandle source, AudioParamHandle target);
        void audio_set_buffer(AudioNodeHandle node, AudioBufferHandle buffer);
        void audio_set_loop(AudioNodeHandle node, bool loop);
        void audio_set_filter_kind(AudioNodeHandle node, BiquadFilterKind kind);
        void audio_set_oscillator_wave(AudioNodeHandle node, OscillatorWave wave);
        void audio_node_start(AudioNodeHandle node, double time);
        void audio_node_stop(AudioNodeHandle node, double time);
    }

    struct AudioEngineHandle
    {
        bl_AudioEngine native;
        pal::AudioContextHandle context;
    };

    namespace babylon
    {
        Engine createEngine(const EngineOptions& options);
        Scene createSceneContext(Engine& engine);
        CameraHandle createFreeCamera(Engine& engine, Vec3d position, Vec3d target);
        SceneNodeHandle createTransformNode(Engine& engine, const std::string& name, Vec3d position,
                                            Vec4 quaternion, Vec3 scaling);
        MeshHandle createBox(Engine& engine, BoxOptions options);
        MeshData createSphereData(SphereOptions options);
        MeshHandle createMeshFromData(Engine& engine, const std::string& name,
                                      const js::F32Array& positions, const js::F32Array& normals,
                                      const js::U32Array& indices, std::optional<js::F32Array> uvs,
                                      std::optional<js::F32Array> uvs2,
                                      std::optional<js::F32Array> tangents,
                                      std::optional<js::F32Array> colors);
        MaterialHandle createShaderMaterial(Engine& engine, uint32_t variant);
        StoredTexture createTexture2DFromPixels(Engine& engine, const js::U8Array& pixels,
                                                double width, double height,
                                                PixelsTextureOptions options);
        void registerScene(Scene& scene);
        void onBeforeRender(Scene& scene, js::Callback<void(double)> callback);

        template<class T> void addToScene(Scene& scene, T handle)
        {
            Check(bl_addToScene(scene.handle, Node(handle)));
        }

        template<class T> void removeFromScene(Scene& scene, T handle)
        {
            Check(bl_removeFromScene(scene.handle, Node(handle)));
        }

        void setSubtreeVisible(Engine& engine, SceneNodeHandle node, bool visible);

        template<class T> void setSubtreeVisible(Engine& engine, T handle, bool visible)
        {
            setSubtreeVisible(engine, Node(handle), visible);
        }

        void setShaderTexture(Engine& engine, MaterialHandle material, uint32_t slot,
                              StoredTexture texture);
        void SetSceneUniform(Engine& engine, uint32_t variant, uint32_t offset, bl_F32Span values);

        template<class... T>
        void setSceneShaderUniform(Engine& engine, uint32_t variant, uint32_t offset, T... values)
        {
            const float data[] = {static_cast<float>(values)...};
            SetSceneUniform(engine, variant, offset, {data, sizeof...(values)});
        }

        AudioEngineHandle createAudioEngineAsync(bl_Runtime* runtime);
        void unlockAudioEngineAsync(AudioEngineHandle engine);
        bl_AudioInputSource createSoundSourceAsync(AudioEngineHandle engine,
                                                   pal::AudioNodeHandle node);
    }

    MaterialHandle remember_scene_material(Engine& engine, uint32_t variant,
                                           MaterialHandle material);
    void SetUniform(Engine& engine, MaterialHandle material, uint32_t offset, bl_F32Span values);

    template<class... T>
    void set_shader_uniform_value(Engine& engine, MaterialHandle material, uint32_t offset,
                                  T... values)
    {
        const float data[] = {static_cast<float>(values)...};
        SetUniform(engine, material, offset, {data, sizeof...(values)});
    }

    void set_mesh_material(Engine& engine, MeshHandle mesh, MaterialHandle material);
    void mark_mesh_dirty(Engine& engine, MeshHandle mesh);
    void set_mesh_transform_parent(Engine& engine, MeshHandle mesh, SceneNodeHandle parent);
    void push_transform_node_child(Engine& engine, SceneNodeHandle parent, MeshHandle mesh);
    void set_scene_node_position_component(Engine& engine, SceneNodeHandle node, uint32_t component,
                                           double value);
    void set_scene_node_rotation(Engine& engine, SceneNodeHandle node, Vec3 value);

    template<class T> void add_to_scene(Scene& scene, T handle)
    {
        babylon::addToScene(scene, handle);
    }

    std::string asset_path(const std::string& name);
    void start_engine(Engine& engine);

    struct PlatformKeyboardEvent
    {
        std::string code;
        std::string key;
        bool repeat{};
        bool shift_key{};
        bool ctrl_key{};
        bool alt_key{};
        bool meta_key{};
        mutable bool default_prevented{};
        std::shared_ptr<DomEventState> dom;
        void prevent_default() const;
    };

    struct PlatformMouseEvent
    {
        double button{};
        double movement_x{};
        double movement_y{};
        double delta_y{};
        mutable bool default_prevented{};
        std::shared_ptr<DomEventState> dom;
        void prevent_default() const;
    };

    void on_dom_keyboard(Engine& engine, DomEventTarget target, const std::string& type,
                         size_t identity, js::Callback<void(const PlatformKeyboardEvent&)> callback,
                         bool capture, bool once, bool passive);
    void on_dom_pointer(Engine& engine, DomEventTarget target, const std::string& type,
                        size_t identity, js::Callback<void(const PlatformMouseEvent&)> callback,
                        bool capture, bool once, bool passive);
    void on_pointer_lock_change(Engine& engine, size_t identity, js::Callback<void()> callback);
    void request_pointer_lock(Engine& engine);
    double set_timeout(Engine& engine, js::Callback<void()> callback, double delayMs);
    void clear_timeout(Engine& engine, double token);

    enum class UiDocumentPart
    {
        Body
    };
    UiElementHandle ui_create_element(Engine& engine, const std::string& tag);
    UiElementHandle ui_document_root(Engine& engine, UiDocumentPart part);
    void ui_append_child(Engine& engine, UiElementHandle parent, UiElementHandle child);
    void ui_append_to_root(Engine& engine, UiElementHandle child);
    void ui_canvas_set_width(Engine& engine, UiElementHandle element, double width);
    void ui_canvas_set_height(Engine& engine, UiElementHandle element, double height);
    void ui_set_attribute(Engine& engine, UiElementHandle element, const std::string& name,
                          const std::string& value);
    void ui_set_style_property(Engine& engine, UiElementHandle element, const std::string& name,
                               const std::string& value);
    void ui_set_inner_rml(Engine& engine, UiElementHandle element, const std::string& value);
    void ui_set_text(Engine& engine, UiElementHandle element, const std::string& value);
    void ui_set_download_name(Engine& engine, UiElementHandle element, const std::string& value);
    void ui_set_download_url(Engine& engine, UiElementHandle element, uint32_t url);
    void ui_set_file_accept(Engine& engine, UiElementHandle element, const std::string& value);
    void ui_set_file_input(Engine& engine, UiElementHandle element);
    void ui_on_file_change(Engine& engine, UiElementHandle element, js::Callback<void()> callback);
    void ui_click(Engine& engine, UiElementHandle element);
}

namespace bbl::js
{
    struct BlobPart
    {
        std::string value;
    };

    BlobPart blob_part_string(const std::string& value);

    struct Blob
    {
        std::string bytes;
        Blob(std::initializer_list<BlobPart> parts, const std::string& type);
    };

    uint32_t create_object_url(Engine& engine, const Blob& blob);
    void revoke_object_url(Engine& engine, uint32_t url);
    using NativeFile = std::shared_ptr<std::string>;
    std::vector<NativeFile> input_files(Engine& engine, UiElementHandle element);
    NativeFile file_at(const std::vector<NativeFile>& files, uint32_t index);

    class FileReader
    {
        struct State
        {
            Nullable<std::string> result;
            Callback<void()> onload;
            Callback<void()> onerror;

            void gc_trace(const TraceVisitor& visitor) const
            {
                visitor(onload);
                visitor(onerror);
            }
        };

        std::shared_ptr<State> state = make_gc_shared<State>();

    public:
        Nullable<std::string> result() const;
        void set_onload(Callback<void()> callback);
        void set_onerror(Callback<void()> callback);
        void read_as_text(Engine& engine, const NativeFile& file);

        void gc_trace(const TraceVisitor& visitor) const
        {
            visitor(state);
        }
    };
}

namespace LiteMinecraft
{
    struct WorldSnapshot
    {
        size_t chunks{};
        size_t nonzeroBlocks{};
        uint64_t hash{1469598103934665603ull};
        size_t mobs{};
        bl_Vec3 cameraPosition{};
        bl_Vec3 cameraTarget{};
        bl_Vec3 playerPosition{};
        double timeOfDay{};
        double seed{};
        size_t edits{};
    };

    void SetWorldProbe(std::function<WorldSnapshot()> probe);
    void ReleaseEngines();
    void RunAdapterContract();
    void RegisterAudioEngine(bl_AudioEngine engine);
    bl_Runtime* Runtime();
    bl_NativeEngineOptions NativeOptions();
    const bl_AudioService& AudioService();
    void Run(bbl::Engine& engine);
}
