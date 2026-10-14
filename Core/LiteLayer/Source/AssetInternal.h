#ifndef BL_ASSET_INTERNAL_H
#define BL_ASSET_INTERNAL_H

#include "LiteInternal.h"

struct L_Asset
{
    L_Record record;
    L_Engine* engine;
    bl_SceneNode root;
    bl_SceneNode* nodes;
    size_t nodeCount;
    size_t nodeCapacity;
    bl_Material* materials;
    size_t materialCount;
    size_t materialCapacity;
    bl_Texture2D* textures;
    size_t textureCount;
};

bl_Status l_createPbrFactorMaterial(bl_EngineContext engine, const bl_AssetPbrDefinition* source,
                                    bl_Material* material);
bl_Status l_composeAssetMatrix(const bl_AssetNode* source, bl_TransformNodeOptions* options);
bl_Status l_validateAsset(const bl_ParsedGltf* data, const char** reason = NULL,
                          const bl_ParsedGltf3* extended = NULL);
bl_Status l_validateAssetHierarchy(bl_Runtime* runtime, const bl_ParsedGltf* data);
bl_Status l_validateAssetTextures(const bl_ParsedGltf3* data, const char** reason);
bl_Status l_applyAssetMaterialInputs(L_Asset* asset, const bl_ParsedGltf3* data, uint32_t index,
                                     bl_Material material);

static_assert(__is_trivial(bl_ParsedGltf3) && __is_standard_layout(bl_ParsedGltf3),
              "Extended raw document must remain POD");
static_assert(offsetof(bl_ParsedGltf3, document) == 0,
              "Raw v3 callback boundary uses the original first-member prefix");
static_assert(__is_trivial(bl_AssetImage) && __is_standard_layout(bl_AssetImage) &&
                  __is_trivial(bl_AssetTexture) && __is_standard_layout(bl_AssetTexture) &&
                  __is_trivial(bl_AssetSampler) && __is_standard_layout(bl_AssetSampler) &&
                  __is_trivial(bl_AssetTextureInfo) && __is_standard_layout(bl_AssetTextureInfo) &&
                  __is_trivial(bl_AssetMaterialInputs) &&
                  __is_standard_layout(bl_AssetMaterialInputs),
              "Raw texture records must remain POD");

#endif
