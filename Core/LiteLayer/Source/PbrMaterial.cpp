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
    bl_Texture2D base = material->pbrInfo.baseColorTexture;
    bl_Texture2D orm = material->pbrInfo.ormTexture;
    L_Record* baseRecord = l_peek(runtime, base._id);
    L_Record* ormRecord = l_peek(runtime, orm._id);
    if (baseRecord && ((L_Texture*)baseRecord)->references)
    {
        --((L_Texture*)baseRecord)->references;
    }
    if (ormRecord && ((L_Texture*)ormRecord)->references)
    {
        --((L_Texture*)ormRecord)->references;
    }
    l_materialCleanup(runtime, record);
    bl_disposeTexture2D(base);
    bl_disposeTexture2D(orm);
    record->disposed = true;
}

bl_Status l_createPbrFactorMaterial(bl_EngineContext engine, const bl_AssetPbrDefinition* source,
                                    bl_Material* out)
{
    bl_Runtime* runtime = engine._runtime;
    bl_Color4 factor =
        source && source->hasBaseColorFactor ? source->baseColorFactor : bl_Color4{1, 1, 1, 1};
    double metallic = source && source->hasMetallicFactor ? source->metallicFactor : 1;
    double roughness = source && source->hasRoughnessFactor ? source->roughnessFactor : 1;
    uint8_t basePixels[4] = {srgbByte(factor.r), srgbByte(factor.g), srgbByte(factor.b),
                             unitByte(factor.a)};
    uint8_t ormPixels[4] = {255, unitByte(roughness), unitByte(metallic), 255};
    bl_PixelsTexture2DOptions options = {BL_ADDRESS_REPEAT, BL_ADDRESS_REPEAT, BL_FILTER_LINEAR,
                                         BL_FILTER_LINEAR, true};
    bl_Texture2D base = {};
    bl_Texture2D orm = {};
    L_TRY(bl_createTexture2DFromPixels(engine, {basePixels, 4}, 1, 1, &options, &base));
    options.srgb = false;
    bl_Status status = bl_createTexture2DFromPixels(engine, {ormPixels, 4}, 1, 1, &options, &orm);
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
    material->depthWrite = true;
    material->pbrInfo = {base, orm, false, {1, 1, 1, 1}, 1,     1,   0.04, 1,
                         1,    1,   0,     false,        false, true};
    material->pbrInfo.doubleSided = !material->culling;
    ++((L_Texture*)l_peek(runtime, base._id))->references;
    ++((L_Texture*)l_peek(runtime, orm._id))->references;
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
