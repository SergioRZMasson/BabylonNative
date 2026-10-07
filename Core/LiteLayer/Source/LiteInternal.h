#ifndef BL_LITE_INTERNAL_H
#define BL_LITE_INTERNAL_H
#include "babylon_lite.h"
#include <bgfx/bgfx.h>
#include <bgfx/defines.h>
#include <bx/os.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

enum L_Kind
{
    L_ENGINE = 1,
    L_SCENE,
    L_NODE,
    L_CAMERA,
    L_MESH,
    L_MATERIAL,
    L_TEXTURE,
    L_AUDIO,
    L_SOURCE,
    L_DATA
};

struct L_Record
{
    uint64_t id;
    uint64_t serial;
    unsigned kind;
    bool disposed;
    int cleanupPriority;
    void (*cleanup)(bl_Runtime*, L_Record*);
    bl_Status (*preDispose)(bl_Runtime*, L_Record*);
    bl_Status cleanupStatus;
    size_t pins;
};

struct L_Slot
{
    uint32_t generation;
    uint32_t kind;
    size_t nextFree;
};

struct bl_Runtime
{
    bl_Allocator allocator;
    bl_ErrorCallback errorCallback;
    void* errorUser;
    bl_ShaderCompilerService compiler;
    bl_AudioService audio;
    bl_IOService io;
    bool hasCompiler;
    bool hasAudio;
    bool hasIO;
    bool destroying;
    uint32_t thread;
    unsigned hostDepth;
    unsigned frameDepth;
    unsigned dispatchDepth;
    L_Record** records;
    L_Slot* slots;
    size_t freeHeads[L_DATA + 1];
    size_t count;
    size_t capacity;
    uint64_t matrixVersion;
    uint64_t callbackId;
    bl_Error error;
    char errorMessage[1024];
};

struct L_Node
{
    L_Record record;
    bl_String name;
    bl_Vec3 position;
    bl_Vec3 scale;
    bl_Vec3 euler;
    bl_Vec3 target;
    bl_Quat quaternion;
    bool eulerValid;
    bool visible;
    bool dirty;
    bool highPrecision;
    L_Node* parent;
    bl_SceneNode* children;
    size_t childCount;
    size_t childCapacity;
    size_t sceneCount;
    bl_Mat4 world;
    uint64_t version;
    uint64_t parentVersion;
    bl_CameraProperties camera;
};

struct L_Uniform
{
    bl_String name;
    bl_ShaderUniformType type;
    bool system;
    uint32_t offset;
    float values[16];
};

struct L_Sampler
{
    bl_String name;
    bl_Texture2D texture;
};

struct L_NativeUniform
{
    bl_String name;
    bl_NativeUniformType type;
    uint16_t count;
    bgfx::UniformHandle handle;
    uint8_t* bytes;
    size_t byteCount;
};

struct L_UniformMap
{
    size_t slot;
    size_t nativeSlot;
    uint32_t offset;
    uint32_t byteSize;
    uint32_t stages;
};

struct L_TextureMap
{
    size_t slot;
    bgfx::UniformHandle handle;
    uint8_t stage;
};

struct L_Material
{
    L_Record record;
    bl_String name;
    bl_String vertex;
    bl_String fragment;
    bl_String prelude;
    bl_VertexSemantic* attributes;
    size_t attributeCount;
    L_Uniform* uniforms;
    size_t uniformCount;
    L_Sampler* samplers;
    size_t samplerCount;
    size_t meshReferences;
    bl_ShaderDefine* defines;
    size_t defineCount;
    bool blending;
    bool testing;
    bool culling;
    bool depthWrite;
    bl_BlendMode blend;
    bl_DepthCompare depthCompare;
    bl_Topology topology;
    double bias;
    double slope;
    struct L_Engine* engine;
    bgfx::ProgramHandle program;
    L_NativeUniform* nativeUniforms;
    size_t nativeUniformCount;
    L_UniformMap* maps;
    size_t mapCount;
    L_TextureMap* textureMaps;
    size_t textureMapCount;
    uint32_t activeAttributes;
    void (*graphicsCleanup)(bl_Runtime*, L_Material*);
};

struct L_Geometry
{
    float* streams[6];
    uint32_t* indices;
    size_t vertices;
    size_t indexCount;
    size_t vertexCapacity;
    size_t indexCapacity;
    bgfx::DynamicVertexBufferHandle vertexBuffer;
    bgfx::DynamicIndexBufferHandle indexBuffer;
    uint16_t stride;
    uint16_t offsets[6];
    uint32_t attributeMask;
    uint8_t* gpuVertices;
    bl_Vec3 minimum;
    bl_Vec3 maximum;
};

struct L_Mesh
{
    L_Node node;
    struct L_Engine* engine;
    L_Geometry geometry;
    bl_MeshProperties properties;
};

struct L_Callback
{
    L_Callback* next;
    uint64_t id;
    bl_BeforeRenderCallback before;
    bl_SceneDisposeCallback dispose;
    void* user;
};

struct L_Scene
{
    L_Record record;
    struct L_Engine* engine;
    bl_SceneProperties properties;
    L_Node** members;
    bool* memberOwners;
    size_t memberCount;
    size_t memberCapacity;
    L_Callback* before;
    L_Callback* dispose;
    bool registered;
    L_Scene* next;
    void* drawScratch;
    size_t drawCapacity;
};

struct L_Engine
{
    L_Record record;
    bl_Runtime* runtime;
    bl_NativeEngineOptions native;
    bl_RendererBackend backend;
    bool highPrecision;
    bool running;
    bool first;
    bool inFrame;
    L_Scene* scenes;
    L_Scene* lastScene;
    L_Engine* globalNext;
    bl_CompletionCallback completion;
    void* completionUser;
    bl_EngineStats stats;
};

struct L_Texture
{
    L_Record record;
    L_Engine* engine;
    bl_Texture2DInfo info;
    uint64_t flags;
    bgfx::TextureHandle handle;
    size_t references;
};

struct L_Audio
{
    L_Record record;
    bl_HostAudioContext context;
    bl_HostAudioNode master;
    bl_HostAudioNode bus;
    double volume;
    double rampDuration;
    double retryInterval;
    double lastPoll;
    double nextRetry;
    bool offline;
    bool resumeInteraction;
    bool resumePause;
    bool started;
    bool hasPoll;
    bool paused;
};

struct L_Source
{
    L_Record record;
    L_Audio* engine;
    bl_HostAudioNode input;
    bl_HostAudioNode gain;
    bl_String name;
    double volume;
};

struct L_Data
{
    L_Record record;
    bl_GeometryData data;
};

static_assert(__is_trivial(L_Record) && __is_standard_layout(L_Record),
              "Record storage must be POD");
static_assert(__is_trivial(L_Node) && __is_standard_layout(L_Node), "Node storage must be POD");
static_assert(__is_trivial(L_Mesh) && __is_standard_layout(L_Mesh), "Mesh storage must be POD");
static_assert(__is_trivial(L_Geometry) && __is_standard_layout(L_Geometry),
              "Geometry storage must be POD");
static_assert(__is_trivial(L_Engine) && __is_standard_layout(L_Engine),
              "Engine storage must be POD");
static_assert(__is_trivial(L_Material) && __is_standard_layout(L_Material),
              "Material storage must be POD");
static_assert(__is_trivial(L_Texture) && __is_standard_layout(L_Texture),
              "Texture storage must be POD");
static_assert(__is_trivial(L_Audio) && __is_standard_layout(L_Audio), "Audio storage must be POD");
static_assert(__is_trivial(L_Source) && __is_standard_layout(L_Source),
              "Source storage must be POD");
static_assert(__is_trivial(L_Data) && __is_standard_layout(L_Data),
              "Geometry allocation storage must be POD");

bl_Status l_check(bl_Runtime* r);
void l_enterHost(bl_Runtime* r);
void l_leaveHost(bl_Runtime* r);
bool l_inHost();
void l_enterFrame(bl_Runtime* r);
void l_leaveFrame(bl_Runtime* r);
bool l_inFrame();
void l_enterDispatch(bl_Runtime* r);
void l_leaveDispatch(bl_Runtime* r);
bool l_inDispatch();
bl_Status l_error(bl_Runtime* r, bl_Status status, const char* operation, const char* message,
                  uint32_t code = 0);
void* l_alloc(bl_Runtime* r, size_t bytes);
void l_free(bl_Runtime* r, void* p);
bool l_size(size_t a, size_t b, size_t* out);
bool l_span(const void* p, size_t n);
bool l_typedSpan(const void* p, size_t n, size_t alignment);
bool l_string(bl_String s);
bool l_equal(bl_String a, bl_String b);
bool l_name(bl_String a, const char* b);
bool l_identifier(bl_String s);
bl_String l_copyString(bl_Runtime* r, bl_String s);
bl_Status l_record(bl_Runtime* r, size_t bytes, unsigned kind, int priority,
                   void (*cleanup)(bl_Runtime*, L_Record*), L_Record** out);
bl_Status l_get(bl_Runtime* r, uint64_t id, unsigned kind, L_Record** out, bool disposedOK = false);
L_Record* l_peek(bl_Runtime* r, uint64_t id);
void l_pin(L_Record* p);
void l_unpin(L_Record* p);
bl_Status l_node(bl_SceneNode handle, L_Node** out, bool disposedOK = false);
bool l_vec(bl_Vec3 p);
bool l_quat(bl_Quat p);
void l_initNode(L_Node* n);
void l_cleanNode(bl_Runtime* r, L_Node* n);
void l_parent(L_Node* n, L_Node* parent);
void l_multiply(const bl_Mat4* a, const bl_Mat4* b, bl_Mat4* out, bool precise);
void l_identity(bl_Mat4* m);
bl_Status l_world(bl_Runtime* r, L_Node* n);
void l_inverse(const bl_Mat4* m, bl_Mat4* out, bool precise);
void l_meshCleanup(bl_Runtime* r, L_Record* record);
void l_geometryCleanup(bl_Runtime* r, L_Geometry* g);
bl_Status l_prepareMaterial(bl_Runtime* r, L_Engine* engine, L_Material* m);
void l_materialCleanup(bl_Runtime* r, L_Record* record);
bl_Status l_drawMaterial(bl_Runtime* r, L_Engine* e, L_Material* m, L_Mesh* mesh,
                         const bl_Mat4* view, const bl_Mat4* projection, bl_Vec3 cameraPosition,
                         uint16_t viewId);
bl_Status l_retire(L_Engine* e);
void l_sceneCleanup(bl_Runtime* r, L_Record* record);
bl_Status l_removeSceneNode(bl_Runtime* r, L_Scene* s, L_Node* n);
bl_Status l_audioDispose(bl_Runtime* r, L_Audio* a);

#define L_TRY(call)                   \
    do                                \
    {                                 \
        bl_Status l_status_ = (call); \
        if (l_status_ != BL_OK)       \
        {                             \
            return l_status_;         \
        }                             \
    }                                 \
    while (0)
#define L_FAIL(r, status, message) l_error((r), (status), __func__, (message))
#define L_CALL(r, status, expression) \
    do                                \
    {                                 \
        l_enterHost(r);               \
        (status) = (expression);      \
        l_leaveHost(r);               \
    }                                 \
    while (0)
#define L_GET(handle, kind, type, variable)                                    \
    L_Record* variable##_record;                                               \
    L_TRY(l_get((handle)._runtime, (handle)._id, (kind), &variable##_record)); \
    type* variable = (type*)variable##_record
#define L_RETIRED(handle, kind, type, variable)                                      \
    L_Record* variable##_record;                                                     \
    L_TRY(l_get((handle)._runtime, (handle)._id, (kind), &variable##_record, true)); \
    type* variable = (type*)variable##_record
#define L_NEW(r, kind, priority, cleanup, type, variable)                                  \
    L_Record* variable##_record;                                                           \
    L_TRY(l_record((r), sizeof(type), (kind), (priority), (cleanup), &variable##_record)); \
    type* variable = (type*)variable##_record
#define L_NODE_GET(handle, variable) \
    L_Node* variable;                \
    L_TRY(l_node((handle), &variable))
#define L_NULL(handle) ((handle)._runtime == NULL && (handle)._id == 0)
#endif
