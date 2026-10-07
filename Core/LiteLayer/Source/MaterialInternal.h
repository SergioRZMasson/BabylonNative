#ifndef BL_MATERIAL_INTERNAL_H
#define BL_MATERIAL_INTERNAL_H

#include "RuntimeInternal.h"

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
    uint16_t programIndex;
    struct L_NativeUniform* nativeUniforms;
    size_t nativeUniformCount;
    struct L_UniformMap* maps;
    size_t mapCount;
    struct L_TextureMap* textureMaps;
    size_t textureMapCount;
    uint32_t activeAttributes;
    void (*graphicsCleanup)(bl_Runtime*, L_Material*);
};

struct L_Texture
{
    L_Record record;
    struct L_Engine* engine;
    bl_Texture2DInfo info;
    uint64_t flags;
    uint16_t handleIndex;
    size_t references;
};

struct L_Text
{
    char* data;
    size_t length;
    size_t capacity;
};

static const size_t uniformCounts[7] = {1, 1, 1, 2, 3, 4, 16};
static const uint8_t attributeComponents[6] = {3, 3, 2, 2, 4, 4};
static const char* attributeNames[6] = {"position", "normal", "uv", "uv2", "tangent", "color"};

static_assert(__is_trivial(L_Material) && __is_standard_layout(L_Material),
              "Material storage must be POD");
static_assert(__is_trivial(L_Texture) && __is_standard_layout(L_Texture),
              "Texture storage must be POD");
static_assert(offsetof(L_Material, record) == 0 && offsetof(L_Texture, record) == 0,
              "Checked records must be convertible through their first member");

size_t l_uniformSlot(L_Material* m, bl_String name);
size_t l_samplerSlot(L_Material* m, bl_String name);
bool l_samplerSuffix(bl_String a, bl_String b);
bool l_appendText(bl_Runtime* r, L_Text* t, bl_String s);
void l_materialCleanup(bl_Runtime* r, L_Record* record);

#endif
