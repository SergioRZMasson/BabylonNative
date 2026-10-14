#ifndef BABYLON_LITE_ASSET_DECODER_H
#define BABYLON_LITE_ASSET_DECODER_H

#include <babylon_lite.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct bl_AssetDecoder bl_AssetDecoder;

/*
 * Separate optional native provider, not an additional engine header/API.
 * Caller keeps decoder and allocator userdata alive through every runtime using
 * its copied table. Destroy only after those runtimes and all results are gone.
 * Parser's private nlohmann allocations are not Core allocator accounting.
 */
bl_Status bl_createAssetDecoder(const bl_Allocator* allocator, bl_AssetDecoder** decoder,
                                bl_AssetService* service);
void bl_destroyAssetDecoder(bl_AssetDecoder* decoder);
/* Advertises full caller-owned GLB document3 storage; environment remains v2. */
bl_Status bl_assetDecoderService3(bl_AssetDecoder* decoder, bl_AssetService* service);
/* Explicit full nested GLB document4 storage; old factories/environment frozen. */
bl_Status bl_assetDecoderService4(bl_AssetDecoder* decoder, bl_AssetService* service);
/* Explicit nested document5 material supplements; old factories/environment frozen. */
bl_Status bl_assetDecoderService5(bl_AssetDecoder* decoder, bl_AssetService* service);

#ifdef __cplusplus
}
#endif

#endif
