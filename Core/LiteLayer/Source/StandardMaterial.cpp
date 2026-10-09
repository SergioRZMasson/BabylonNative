#include "MaterialInternal.h"
#include "SceneInternal.h"

bl_Status l_materialFamily(bl_Material h, L_Material** out)
{
    L_Record* record;
    L_TRY(l_get(h._runtime, h._id, 0, &record));
    if (record->kind != L_MATERIAL && record->kind != L_STANDARD)
    {
        return BL_INVALID_HANDLE;
    }
    *out = (L_Material*)record;
    return BL_OK;
}

bl_Status bl_createStandardMaterial(bl_Runtime* r, bl_StandardMaterial* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_NEW(r, L_STANDARD, 20, l_materialCleanup, L_Material, material);
    material->programIndex = BL_INVALID_BGFX_HANDLE;
    material->standardProperties = {{1, 1, 1}, 1, {1, 1, 1}, 64, {}, {}, true, false};
    material->culling = true;
    material->depthWrite = true;
    *out = {r, material->record.id};
    return BL_OK;
}

bl_Status bl_getStandardMaterialProperties(bl_StandardMaterial h,
                                           bl_StandardMaterialProperties* out)
{
    L_GET(h, L_STANDARD, L_Material, material);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = material->standardProperties;
    return BL_OK;
}

bl_Status bl_setStandardMaterialProperties(bl_StandardMaterial h,
                                           const bl_StandardMaterialProperties* p)
{
    L_GET(h, L_STANDARD, L_Material, material);
    if (!p || !l_vec(p->diffuseColor) || !isfinite(p->alpha) || !l_vec(p->specularColor) ||
        !isfinite(p->specularPower) || !l_vec(p->emissiveColor) || !l_vec(p->ambientColor))
    {
        return BL_INVALID_ARGUMENT;
    }
    material->standardProperties = *p;
    return BL_OK;
}

bl_Status bl_markMaterialUboDirty(bl_Material h)
{
    L_Material* material;
    L_TRY(l_materialFamily(h, &material));
    ++material->uboVersion;
    return BL_OK;
}

bl_Status bl_shaderMaterialAsMaterial(bl_ShaderMaterial h, bl_Material* out)
{
    L_GET(h, L_MATERIAL, L_Material, material);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, material->record.id};
    return BL_OK;
}

bl_Status bl_standardMaterialAsMaterial(bl_StandardMaterial h, bl_Material* out)
{
    L_GET(h, L_STANDARD, L_Material, material);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, material->record.id};
    return BL_OK;
}

bl_Status bl_materialAsShaderMaterial(bl_Material h, bl_ShaderMaterial* out)
{
    L_GET(h, L_MATERIAL, L_Material, material);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, material->record.id};
    return BL_OK;
}

bl_Status bl_materialAsStandardMaterial(bl_Material h, bl_StandardMaterial* out)
{
    L_GET(h, L_STANDARD, L_Material, material);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, material->record.id};
    return BL_OK;
}

bl_Status bl_disposeStandardMaterial(bl_StandardMaterial h)
{
    L_RETIRED(h, L_STANDARD, L_Material, material);
    if (!material || material->record.disposed)
    {
        return BL_OK;
    }
    if (material->meshReferences || l_inDispatch())
    {
        return BL_BUSY;
    }
    l_materialCleanup(h._runtime, &material->record);
    material->record.disposed = true;
    return BL_OK;
}
