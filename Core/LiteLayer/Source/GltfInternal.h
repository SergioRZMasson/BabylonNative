#ifndef BL_GLTF_INTERNAL_H
#define BL_GLTF_INTERNAL_H

#include "AssetInternal.h"
#include "GltfParserPrivate.h"

struct L_GltfAllocation
{
    L_GltfAllocation* previous;
    L_GltfAllocation* next;
};

struct L_Gltf
{
    bl_Runtime* runtime;
    cgltf_data* data;
    cgltf_options options;
    bl_Status ioStatus;
    const char* reason;
    const char* path;
    L_GltfAllocation* allocations;
    jmp_buf parseJump;
    bool parseGuard;
};

bl_Status l_gltfParse(L_Gltf* gltf, const uint8_t* input, size_t bytes);
void l_gltfFreeAllocations(L_Gltf* gltf);
bl_Status l_gltfResult(L_Gltf* gltf, cgltf_result result);
bl_Status l_gltfValidateAccessors(L_Gltf* gltf);
bl_Status l_gltfRead(const cgltf_accessor* accessor, size_t index, float* out, size_t count);
bl_Status l_gltfMaterial(L_Gltf* gltf, L_Asset* asset, const cgltf_material* source,
                         bl_Material* out);
bl_Status l_gltfMesh(L_Gltf* gltf, L_Asset* asset, const cgltf_node* node,
                     const cgltf_primitive* primitive, bl_TransformNode parent);
bl_Status l_gltfReadUri(L_Gltf* gltf, const char* uri, uint8_t** out, size_t* bytes);
bl_Status l_gltfValidateMotion(L_Gltf* gltf);
bl_Status l_gltfCreateMotion(L_Gltf* gltf, L_Asset* asset);
bl_Status l_gltfBindSkin(L_Gltf* gltf, L_Asset* asset, const cgltf_node* node,
                         const cgltf_primitive* primitive, L_Mesh* mesh);
const cgltf_accessor* l_gltfInfluence(const cgltf_primitive* primitive, size_t set, bool joints);
bl_Status l_gltfInfluenceSets(const cgltf_primitive* primitive, size_t* sets, size_t* vertices);

#endif
