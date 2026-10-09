#include "HemisphericLightInternal.h"
#include "DirectionalLightInternal.h"

bl_Status l_lightFamily(bl_Light h, L_Light** out)
{
    L_Record* record;
    L_TRY(l_get(h._runtime, h._id, L_LIGHT, &record));
    L_Light* light = (L_Light*)record;
    if (light->kind != L_LIGHT_HEMISPHERIC && light->kind != L_LIGHT_DIRECTIONAL)
    {
        return BL_INVALID_HANDLE;
    }
    *out = light;
    return BL_OK;
}

bl_Status bl_setLightIntensity(bl_Light h, double value)
{
    L_Light* light;
    L_TRY(l_lightFamily(h, &light));
    if (!isfinite(value))
    {
        return l_error(h._runtime, BL_INVALID_ARGUMENT, __func__, "Nonfinite intensity", 134);
    }
    double* intensity = light->kind == L_LIGHT_HEMISPHERIC
                            ? &((L_HemisphericLight*)light)->properties.intensity
                            : &((L_DirectionalLight*)light)->properties.intensity;
    if (value != *intensity)
    {
        *intensity = value;
        ++light->dataVersion;
    }
    return BL_OK;
}

bl_Status bl_markLightUboDirty(bl_Light h)
{
    L_Light* light;
    L_TRY(l_lightFamily(h, &light));
    ++light->dataVersion;
    return BL_OK;
}

bl_Status bl_lightNode(bl_Light h, bl_SceneNode* out)
{
    L_Light* light;
    L_TRY(l_lightFamily(h, &light));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, light->node.record.id};
    return BL_OK;
}

bl_Status l_writeLight(bl_Runtime* r, L_Light* light, float* data)
{
    if (light->kind == L_LIGHT_HEMISPHERIC)
    {
        return l_writeHemisphericLight(r, (L_HemisphericLight*)light, data);
    }
    if (light->kind == L_LIGHT_DIRECTIONAL)
    {
        return l_writeDirectionalLight(r, (L_DirectionalLight*)light, data);
    }
    return BL_INVALID_HANDLE;
}
