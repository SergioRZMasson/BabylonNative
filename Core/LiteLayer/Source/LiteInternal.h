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
    uint32_t* gpuIndices;
    bool dedicatedIndices;
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
    struct L_CpuGeometryOwner* cpuOwner;
    struct L_GpuGeometryOwner* gpuOwner;
};

struct L_Mesh
{
    L_Node node;
    L_Engine* engine;
    L_Geometry geometry;
    bl_MeshProperties properties;
    bl_Material material;
    float* instanceMatrices;
    double* instanceMatrices64;
    float* instanceColors;
    size_t instanceColorCount;
    uint64_t instanceColorVersion;
    size_t instanceDirtyMin;
    size_t instanceDirtyMax;
    size_t instanceColorDirtyMin;
    size_t instanceColorDirtyMax;
    bool instanceCulling;
    uint16_t instanceGpuStride;
    size_t instanceGpuCapacity;
    float* instanceUpload;
    size_t instanceUploadFloats;
    size_t instanceFloatCount;
    size_t instanceCount;
    size_t instanceCapacity;
    uint64_t instanceVersion;
    uint64_t instanceGpuVersion;
    bool hasInstances;
    bool instanceWorldBounds;
    bool assetFlatNormal;
    uint16_t instanceBufferIndex;
    bool mirroredReady;
    bool mirrored;
    uint64_t mirroredVersion;
    bl_Topology topology;
    uint32_t* linePointCounts;
    size_t lineCount;
    struct L_AssetDeformation* skin;
    struct L_ThinOwner* thinOwner;
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
    bool alphaToCoverage;
};

struct L_DrawGroup
{
    uint64_t materialId;
    double order;
    size_t sequence;
};

struct L_ThinDraw
{
    bgfx::DynamicVertexBufferHandle instances;
    bgfx::IndirectBufferHandle indirect;
};

struct L_StorageUpload
{
    bgfx::VertexBufferHandle source;
    uint16_t destinationIndex;
    uint16_t destinationType;
    uint32_t offset;
    uint32_t size;
};

static_assert(__is_trivial(L_StorageUpload) && __is_standard_layout(L_StorageUpload),
              "Prepared uploads must remain POD; bgfx regions are stack temporaries");
static_assert(__is_trivial(L_Mesh) && __is_standard_layout(L_Mesh), "Mesh storage must be POD");
static_assert(__is_trivial(L_Geometry) && __is_standard_layout(L_Geometry),
              "Geometry storage must be POD");
static_assert(offsetof(L_Mesh, node) == 0 && offsetof(L_Node, record) == 0,
              "Checked records must be convertible through their first member");

void l_meshCleanup(bl_Runtime* r, L_Record* record);
bl_Status l_tickAssetMotion(struct L_Asset* asset, L_Scene* scene, double delta);
void l_detachAssetSkin(L_Mesh* mesh);
bl_Status l_meshWorldBounds(bl_Runtime* runtime, L_Mesh* mesh, bl_Vec3* minimum, bl_Vec3* maximum,
                            bool* hasBounds);
void l_geometryCleanup(bl_Runtime* r, L_Geometry* g);
void l_releaseGeometryStorage(bl_Runtime* runtime, L_Geometry* geometry);
void l_syncGeometryGpu(L_Geometry* geometry);
void l_replaceSharedGpuMirrors(bl_Runtime* runtime, L_Geometry* oldGeometry,
                               L_Geometry* nextGeometry);
void l_publishGeometryGpu(L_Geometry* geometry);
void l_publishThinState(bl_Runtime* runtime, L_Mesh* mesh);
bool l_releaseThinOwner(bl_Runtime* runtime, L_Mesh* mesh);
bl_Status l_cloneMeshRecord(bl_Runtime* runtime, L_Record* source, bl_String name,
                            L_Record** clone);
bl_Status l_prepareMaterial(bl_Runtime* r, L_Engine* engine, L_Material* m);
bl_Status l_drawMaterial(bl_Runtime* r, L_Engine* e, L_Scene* scene, L_Material* m, L_Mesh* mesh,
                         const bl_Mat4* view, const bl_Mat4* projection, bl_Vec3 cameraPosition,
                         uint16_t viewId, L_StandardPacket* standard);
bl_Status l_collectDraws(bl_Runtime* r, L_Scene* s, const bl_Mat4* view, size_t* count);
void l_sortDraws(L_Scene* s, size_t count);
bl_Material l_meshMaterial(const L_Mesh* mesh);
bl_Status l_checkStandardGeometry(bl_Runtime* runtime, const L_Material* material,
                                  const L_Mesh* mesh);
bl_Status l_createMeshTopology(bl_EngineContext engine, bl_String name,
                               const bl_MeshGeometry* geometry, bl_Topology topology,
                               bl_Mesh* mesh);
bl_Status l_prepareMeshMaterial(bl_Runtime* runtime, L_Engine* engine, L_Material* material,
                                L_Mesh* mesh, L_Material** pipeline);
bl_Status l_prepareThinBuffer(bl_Runtime* runtime, L_Mesh* mesh, bool colors);
bl_Status l_validateStorageUploadViews(L_Engine* engine, size_t count);
bl_Status l_prepareStorageUpload(const bgfx::BufferRegion* destination, const void* bytes,
                                 L_StorageUpload* upload);
void l_cancelStorageUploads(L_StorageUpload* uploads, size_t count);
/* Submission consumes every source, including on failure. */
bl_Status l_submitStorageUploads(L_Engine* engine, L_StorageUpload* uploads, size_t count);
bl_Status l_prepareThinCulling(bl_Runtime* runtime, L_Mesh* mesh, bool colors);
bl_Status l_prepareThinDraw(bl_Runtime* runtime, L_Scene* scene, L_Mesh* mesh, const bl_Mat4* view,
                            const bl_Mat4* projection, uint16_t viewId, bool colors, bool excluded,
                            L_ThinDraw* draw);
bl_Status l_snapshotAlpha(bl_Runtime* runtime, L_Scene* scene, const L_Material* material);
bl_Status l_prepareStandard(bl_Runtime* runtime, L_Engine* engine, L_Material* material,
                            bool disableLighting, L_Material** pipeline,
                            unsigned instanceSlots = 0);
bl_Status l_prepareStandardPackets(bl_Runtime* runtime, L_Scene* scene, bool force,
                                   uint64_t materialId);
bl_Status l_standardValues(bl_Runtime* runtime, L_Engine* engine, L_Scene* scene,
                           L_StandardPacket* packet, L_Mesh* mesh, const bl_Mat4* view,
                           const bl_Mat4* projection, bl_Vec3 cameraPosition);

#endif
