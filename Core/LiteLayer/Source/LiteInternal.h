#ifndef BL_LITE_INTERNAL_H
#define BL_LITE_INTERNAL_H

#include "SceneInternal.h"
#include "MaterialInternal.h"
#include <bgfx/bgfx.h>
#include <bgfx/defines.h>
#include <stdio.h>

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
    L_Engine* engine;
    L_Geometry geometry;
    bl_MeshProperties properties;
    bl_Material material;
};

struct L_Draw
{
    L_Mesh* mesh;
    L_Material* material;
    double order;
    double depth;
    size_t sequence;
    size_t group;
    bool transparent;
    struct L_StandardPacket* standard;
};

struct L_StandardPacket
{
    uint64_t meshId;
    uint64_t materialId;
    uint64_t uboVersion;
    bl_StandardMaterialProperties properties;
    L_Material* pipeline;
    bool transparent;
    bool culling;
};

struct L_DrawGroup
{
    uint64_t materialId;
    double order;
    size_t sequence;
};

static_assert(__is_trivial(L_Mesh) && __is_standard_layout(L_Mesh), "Mesh storage must be POD");
static_assert(__is_trivial(L_Geometry) && __is_standard_layout(L_Geometry),
              "Geometry storage must be POD");
static_assert(offsetof(L_Mesh, node) == 0 && offsetof(L_Node, record) == 0,
              "Checked records must be convertible through their first member");

void l_meshCleanup(bl_Runtime* r, L_Record* record);
void l_geometryCleanup(bl_Runtime* r, L_Geometry* g);
bl_Status l_prepareMaterial(bl_Runtime* r, L_Engine* engine, L_Material* m);
bl_Status l_drawMaterial(bl_Runtime* r, L_Engine* e, L_Scene* scene, L_Material* m, L_Mesh* mesh,
                         const bl_Mat4* view, const bl_Mat4* projection, bl_Vec3 cameraPosition,
                         uint16_t viewId, L_StandardPacket* standard);
bl_Status l_collectDraws(bl_Runtime* r, L_Scene* s, const bl_Mat4* view, size_t* count);
void l_sortDraws(L_Scene* s, size_t count);
bl_Material l_meshMaterial(const L_Mesh* mesh);
bl_Status l_checkStandardGeometry(bl_Runtime* runtime, const L_Material* material,
                                  const L_Mesh* mesh);
bl_Status l_prepareStandard(bl_Runtime* runtime, L_Engine* engine, L_Material* material,
                            bool disableLighting, L_Material** pipeline);
bl_Status l_prepareStandardPackets(bl_Runtime* runtime, L_Scene* scene, bool force,
                                   uint64_t materialId);
bl_Status l_standardValues(bl_Runtime* runtime, L_Engine* engine, L_Scene* scene,
                           L_StandardPacket* packet, L_Mesh* mesh, const bl_Mat4* view,
                           const bl_Mat4* projection, bl_Vec3 cameraPosition);

#endif
