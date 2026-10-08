#ifndef BL_UI_INTERNAL_H
#define BL_UI_INTERNAL_H

#include "babylon_lite.h"
#include "SceneInternal.h"
#include <bgfx/bgfx.h>
#include <bgfx/defines.h>

enum U_ResourceKind
{
    U_GEOMETRY = 1,
    U_TEXTURE,
    U_GRADIENT,
    U_FILTER
};

enum
{
    U_PROGRAM_COUNT = 4,
    U_MAX_LAYERS = 8,
    U_MAX_CLIP_COMMANDS = 16
};

struct U_Surface
{
    bgfx::TextureHandle color;
    bgfx::TextureHandle depth;
    bgfx::FrameBufferHandle framebuffer;
    uint32_t width;
    uint32_t height;
    uint64_t id;
};

struct U_ClipCommand
{
    uint64_t geometry;
    unsigned operation;
    float translation[2];
    float transform[16];
};

struct U_Resource
{
    U_Resource* next;
    uint64_t handle;
    unsigned kind;
    bool retired;
    bgfx::VertexBufferHandle vertices;
    bgfx::IndexBufferHandle indices;
    bgfx::TextureHandle texture;
    uint32_t indexCount;
    float gradient[18][4];
    struct U_Image* image;
    uint32_t samplerFlags;
    bool whiteSource;
};

struct U_Uniform
{
    bl_String name;
    bgfx::UniformHandle handle;
    bl_NativeUniformType type;
    uint16_t count;
    uint8_t* bytes;
    size_t byteCount;
};

struct U_UniformMap
{
    unsigned source;
    unsigned destination;
    uint32_t offset;
    uint32_t bytes;
};

struct U_Program
{
    bgfx::ProgramHandle handle;
    U_Uniform uniforms[24];
    U_UniformMap maps[24];
    size_t uniformCount;
    size_t mapCount;
    bgfx::UniformHandle sampler;
    uint8_t textureStage;
};

struct U_Image
{
    U_Image* next;
    bl_String source;
    uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    size_t references;
    bool pixelated;
};

struct U_Context
{
    L_Record record;
    bl_Runtime* runtime;
    L_Engine* engine;
    L_ViewReservation views;
    bl_UiContextOptions options;
    bl_UiStats stats;
    U_Context* globalNext;
    void* context;
    void* document;
    void* renderer;
    U_Resource* resources;
    U_Image* images;
    struct U_Listener* listeners;
    U_Program programs[U_PROGRAM_COUNT];
    uint64_t resourceSerial;
    uint64_t rootId;
    bl_Status pending;
    char pendingMessage[512];
    bl_Status renderFailure;
    char renderMessage[512];
    double time;
    bool hasTime;
    bool busy;
    bool closing;
    bool recording;
    bool submitted;
    bool whiteDifference;
    bool scissorEnabled;
    bool clipEnabled;
    uint8_t clipLevel;
    uint32_t stencil;
    bool stencilWriting;
    uint64_t clipQuad;
    uint64_t layerQuad;
    U_Surface layers[U_MAX_LAYERS];
    U_Surface scratch[2];
    unsigned layerDepth;
    uint32_t viewCursor;
    uint16_t currentView;
    unsigned clipCommandCount;
    U_ClipCommand clipCommands[U_MAX_CLIP_COMMANDS];
    bool replayingClip;
    int32_t scissor[4];
    float transform[16];
};

struct U_Element
{
    L_Record record;
    U_Context* owner;
    void* element;
    bool detached;
    bool root;
    bool hasContent;
    bool contentMarkup;
    bl_String content;
    uint64_t parentId;
};

static_assert(__is_trivial(U_Context) && __is_standard_layout(U_Context),
              "Native UI context storage must be POD");
static_assert(__is_trivial(U_Element) && __is_standard_layout(U_Element),
              "Native element identity storage must be POD");
static_assert(offsetof(U_Context, record) == 0 && offsetof(U_Element, record) == 0,
              "Checked UI identities must be convertible through their first member");

void u_pending(U_Context* context, bl_Status status, const char* message);
void u_resourceFailure(U_Context* context, bl_Status status, const char* message);
bl_Status u_finish(U_Context* context, const char* operation);
bl_Status u_gpuCreate(U_Context* context);
void u_gpuDestroy(U_Context* context);
void u_collect(U_Context* context);
U_Resource* u_resource(U_Context* context, uint64_t handle, unsigned kind);
uint64_t u_geometry(U_Context* context, const float* vertices, size_t vertexCount,
                    const uint32_t* indices, size_t indexCount);
uint64_t u_texture(U_Context* context, const uint8_t* pixels, uint32_t width, uint32_t height);
uint64_t u_gradient(U_Context* context, const float parameters[18][4]);
void u_release(U_Context* context, uint64_t handle, unsigned kind);
void u_draw(U_Context* context, uint64_t geometry, const float translation[2], uint64_t texture,
            uint64_t gradient);
void u_begin(U_Context* context);
void u_mask(U_Context* context, unsigned operation, uint64_t geometry, const float translation[2]);
void u_drawRaw(U_Context* context, uint64_t geometry, bgfx::TextureHandle texture,
               const float* blurParameters, bool replace);
void u_layersInit(U_Context* context);
void u_layersDestroy(U_Context* context);
void u_layersResize(U_Context* context);
bool u_layerView(U_Context* context, unsigned layer, uint16_t clearFlags, bool replayClip);
uint64_t u_pushLayer(U_Context* context);
void u_popLayer(U_Context* context);
uint64_t u_compileBlur(U_Context* context, float sigma);
void u_composite(U_Context* context, uint64_t source, uint64_t destination, bool replace,
                 const uint64_t* filters, size_t filterCount);
uint64_t u_saveLayer(U_Context* context);
uint64_t u_textureStorage(U_Context* context, uint32_t width, uint32_t height, uint64_t flags);
bool u_allocateView(U_Context* context, uint16_t* view);
void u_replayClip(U_Context* context);

#endif
