#include "AssetInternal.h"

static uint8_t unitByte(double value)
{
    double clamped = value < 0 ? 0 : value > 1 ? 1 : value;
    return (uint8_t)floor(clamped * 255 + 0.5);
}

static uint8_t srgbByte(double value)
{
    double clamped = value < 0 ? 0 : value > 1 ? 1 : value;
    double encoded = clamped <= 0.0031308 ? clamped * 12.92 : 1.055 * pow(clamped, 1 / 2.4) - 0.055;
    return unitByte(encoded);
}

static void pbrCleanup(bl_Runtime* runtime, L_Record* record)
{
    L_Material* material = (L_Material*)record;
    const bl_Texture2D textures[6] = {
        material->pbrInfo.baseColorTexture, material->pbrInfo.ormTexture,
        material->pbrNormalTexture,         material->pbrOcclusionTexture,
        material->pbrEmissiveTexture,       material->pbrSpecularGlossinessTexture};
    for (unsigned i = 0; i < 6; ++i)
    {
        L_Record* texture = l_peek(runtime, textures[i]._id);
        if (texture && ((L_Texture*)texture)->references)
        {
            --((L_Texture*)texture)->references;
        }
    }
    l_materialCleanup(runtime, record);
    for (unsigned i = 0; i < 6; ++i)
    {
        if (!L_NULL(textures[i]))
        {
            bl_disposeTexture2D(textures[i]);
        }
    }
    record->disposed = true;
}

bl_Status l_createPbrFactorMaterial(bl_EngineContext engine, const bl_AssetPbrDefinition* source,
                                    bl_Material* out,
                                    const bl_AssetSpecularGlossinessInputs* inputs)
{
    bl_Runtime* runtime = engine._runtime;
    bool specGloss = inputs && inputs->present;
    bl_Color4 factor =
        source && source->hasBaseColorFactor ? source->baseColorFactor : bl_Color4{1, 1, 1, 1};
    if (specGloss)
    {
        factor = inputs->hasDiffuseFactor ? inputs->diffuseFactor : bl_Color4{1, 1, 1, 1};
    }
    double metallic = source && source->hasMetallicFactor ? source->metallicFactor : 1;
    double roughness = source && source->hasRoughnessFactor ? source->roughnessFactor : 1;
    uint8_t basePixels[4] = {srgbByte(factor.r), srgbByte(factor.g), srgbByte(factor.b),
                             unitByte(factor.a)};
    uint8_t ormPixels[4] = {255, unitByte(roughness), unitByte(metallic), 255};
    if (specGloss)
    {
        memset(basePixels, 255, sizeof(basePixels));
    }
    bl_PixelsTexture2DOptions options = {BL_ADDRESS_REPEAT, BL_ADDRESS_REPEAT, BL_FILTER_LINEAR,
                                         BL_FILTER_LINEAR, true};
    bl_Texture2D base = {};
    bl_Texture2D orm = {};
    L_TRY(bl_createTexture2DFromPixels(engine, {basePixels, 4}, 1, 1, &options, &base));
    options.srgb = false;
    bl_Status status =
        specGloss ? BL_OK
                  : bl_createTexture2DFromPixels(engine, {ormPixels, 4}, 1, 1, &options, &orm);
    L_Record* record = NULL;
    if (status == BL_OK)
    {
        status = l_record(runtime, sizeof(L_Material), L_PBR, 20, pbrCleanup, &record);
    }
    if (status != BL_OK)
    {
        bl_disposeTexture2D(base);
        if (!L_NULL(orm))
        {
            bl_disposeTexture2D(orm);
        }
        return status;
    }
    L_Material* material = (L_Material*)record;
    material->programIndex = BL_INVALID_BGFX_HANDLE;
    material->culling = !(source && source->hasDoubleSided && source->doubleSided);
    bl_AssetAlphaMode alphaMode =
        source && source->hasAlphaMode ? source->alphaMode : BL_ASSET_ALPHA_OPAQUE;
    material->blending = alphaMode == BL_ASSET_ALPHA_BLEND;
    material->testing = alphaMode == BL_ASSET_ALPHA_MASK;
    material->depthWrite = !material->blending;
    material->pbrInfo = {base, orm, false, {1, 1, 1, 1}, 1,     1,   0.04, 1,
                         1,    1,   0,     false,        false, true};
    material->pbrInfo.doubleSided = !material->culling;
    material->pbrInfo.alphaBlend = material->blending;
    material->pbrInfo.alpha = alphaMode == BL_ASSET_ALPHA_OPAQUE ? 1 : factor.a;
    material->pbrSpecularGlossiness = specGloss;
    if (specGloss)
    {
        material->pbrInfo.baseColorFactor = factor;
        material->pbrInfo.hasBaseColorFactor =
            factor.r != 1 || factor.g != 1 || factor.b != 1 || factor.a != 1;
        material->pbrInfo.alpha = 1;
        material->pbrSpecularFactor =
            inputs->hasSpecularFactor ? inputs->specularFactor : bl_Vec3{1, 1, 1};
        material->pbrGlossinessFactor = inputs->hasGlossinessFactor ? inputs->glossinessFactor : 1;
    }
    material->pbrAlphaCutoff = source && source->hasAlphaCutoff ? source->alphaCutoff : 0.5;
    material->pbrNormalScale = 1;
    material->pbrEmissiveFactor =
        source && source->hasEmissiveFactor ? source->emissiveFactor : bl_Vec3{};
    material->pbrExtra = specGloss || material->pbrEmissiveFactor.x != 0 ||
                         material->pbrEmissiveFactor.y != 0 || material->pbrEmissiveFactor.z != 0;
    ++((L_Texture*)l_peek(runtime, base._id))->references;
    if (!L_NULL(orm))
    {
        ++((L_Texture*)l_peek(runtime, orm._id))->references;
    }
    if (source && source->hasName)
    {
        material->name = l_copyString(runtime, source->name);
        if (!material->name.data)
        {
            pbrCleanup(runtime, record);
            return BL_OUT_OF_MEMORY;
        }
    }
    *out = {runtime, record->id};
    return BL_OK;
}

bl_Status bl_pbrMaterialAsMaterial(bl_PbrMaterial handle, bl_Material* out)
{
    L_GET(handle, L_PBR, L_Material, material);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {handle._runtime, material->record.id};
    return BL_OK;
}

bl_Status bl_materialAsPbrMaterial(bl_Material handle, bl_PbrMaterial* out)
{
    L_GET(handle, L_PBR, L_Material, material);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {handle._runtime, material->record.id};
    return BL_OK;
}

bl_Status bl_getPbrMaterialInfo(bl_PbrMaterial handle, bl_PbrMaterialInfo* out)
{
    L_GET(handle, L_PBR, L_Material, material);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (material->pbrSpecularGlossiness)
    {
        return l_error(handle._runtime, BL_UNSUPPORTED, __func__,
                       "The frozen MR-oriented PBR info record cannot represent the "
                       "specular/glossiness workflow",
                       0);
    }
    *out = material->pbrInfo;
    return BL_OK;
}

bl_Status bl_disposePbrMaterial(bl_PbrMaterial handle)
{
    L_RETIRED(handle, L_PBR, L_Material, material);
    if (!material || material->record.disposed)
    {
        return BL_OK;
    }
    if (material->meshReferences || material->record.pins)
    {
        return BL_BUSY;
    }
    L_Texture* texture =
        (L_Texture*)l_peek(handle._runtime, material->pbrInfo.baseColorTexture._id);
    if (texture && texture->engine)
    {
        L_TRY(l_retire(texture->engine));
    }
    pbrCleanup(handle._runtime, &material->record);
    return BL_OK;
}
