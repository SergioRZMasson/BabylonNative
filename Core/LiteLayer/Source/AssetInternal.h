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
                                    bl_Material* material,
                                    const bl_AssetSpecularGlossinessInputs* inputs = NULL);
bl_Status l_composeAssetMatrix(const bl_AssetNode* source, bl_TransformNodeOptions* options);
bl_Status l_validateAsset(const bl_ParsedGltf* data, const char** reason = NULL,
                          const bl_ParsedGltf3* extended = NULL, const bl_ParsedGltf4* full = NULL,
                          const bl_ParsedGltf5* materials = NULL);
bl_Status l_validateAssetHierarchy(bl_Runtime* runtime, const bl_ParsedGltf* data);
bl_Status l_validateAssetTextures(const bl_ParsedGltf3* data, const char** reason,
                                  const bl_ParsedGltf5* materials = NULL);
bl_Status l_applyAssetMaterialInputs(L_Asset* asset, const bl_ParsedGltf3* data, uint32_t index,
                                     bl_Material material, const bl_ParsedGltf4* full = NULL,
                                     const bl_AssetSpecularGlossinessInputs* inputs = NULL);

struct L_AssetAccessorReader
{
    const bl_AssetAccessor* accessor;
    const uint8_t* base;
    const uint8_t* sparseIndices;
    const uint8_t* sparseValues;
    size_t stride;
    size_t elementBytes;
    size_t sparseCount;
    size_t columns;
    size_t rows;
    size_t columnBytes;
    size_t width;
    size_t indexWidth;
    bool normalized;
};

size_t l_assetViewOffset(const bl_ParsedGltf* data, uint32_t index, const bl_ParsedGltf4* full);
bl_Status l_assetAccessorReader(const bl_ParsedGltf* data, uint32_t index,
                                const bl_ParsedGltf4* full, L_AssetAccessorReader* reader);
bl_Status l_assetAccessorElement(const L_AssetAccessorReader* reader, size_t element,
                                 double* values, bool normalize = true);
bl_Status l_validateAssetAccessorBounds(const bl_ParsedGltf* data, const bl_ParsedGltf4* full);
bl_Status l_assetTriangleIndices(bl_Runtime* runtime, const bl_ParsedGltf* data,
                                 const bl_AssetPrimitive* primitive, size_t vertices,
                                 const bl_ParsedGltf4* full, bl_U32Span* indices);

static_assert(__is_trivial(bl_ParsedGltf5) && __is_standard_layout(bl_ParsedGltf5) &&
                  offsetof(bl_ParsedGltf5, document4) == 0 &&
                  __is_trivial(bl_AssetSpecularGlossinessInputs) &&
                  __is_standard_layout(bl_AssetSpecularGlossinessInputs),
              "Raw v5 material inputs use full checked first-member POD storage");
static_assert(__is_trivial(bl_ParsedGltf4) && __is_standard_layout(bl_ParsedGltf4) &&
                  offsetof(bl_ParsedGltf4, document3) == 0,
              "Raw v4 uses full checked nested first-member storage");
static_assert(__is_trivial(bl_AssetSparseAccessor) &&
                  __is_standard_layout(bl_AssetSparseAccessor) &&
                  __is_trivial(bl_AssetAccessorInputs) &&
                  __is_standard_layout(bl_AssetAccessorInputs) &&
                  __is_trivial(bl_AssetBufferViewInputs) &&
                  __is_standard_layout(bl_AssetBufferViewInputs),
              "Raw accessor supplements must remain POD");

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
