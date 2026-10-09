#include "HemisphericLightInternal.h"

static void lightCleanup(bl_Runtime* r, L_Record* record)
{
    l_cleanNode(r, (L_Node*)record);
}

static bool lightProperties(const bl_HemisphericLightProperties* p)
{
    return p && l_vec(p->direction) && isfinite(p->intensity) && l_vec(p->diffuseColor) &&
           l_vec(p->specularColor) && l_vec(p->groundColor);
}

bl_Status bl_createHemisphericLight(bl_Runtime* r, const bl_HemisphericLightOptions* options,
                                    bl_HemisphericLight* out)
{
    L_TRY(l_check(r));
    bl_Vec3 direction = {0, 1, 0};
    double intensity = 1;
    if (options)
    {
        if (options->hasDirection)
        {
            direction = options->direction;
        }
        if (options->intensity.present)
        {
            intensity = options->intensity.value;
        }
    }
    if (!out || !l_vec(direction) || !isfinite(intensity))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_NEW(r, L_LIGHT, 30, lightCleanup, L_HemisphericLight, light);
    l_initNode(&light->node);
    light->properties = {direction, intensity, {1, 1, 1}, {1, 1, 1}, {0, 0, 0}};
    *out = {r, light->node.record.id};
    return BL_OK;
}

bl_Status bl_getHemisphericLightProperties(bl_HemisphericLight h,
                                           bl_HemisphericLightProperties* out)
{
    L_GET(h, L_LIGHT, L_HemisphericLight, light);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = light->properties;
    return BL_OK;
}

bl_Status bl_setHemisphericLightProperties(bl_HemisphericLight h,
                                           const bl_HemisphericLightProperties* p)
{
    L_GET(h, L_LIGHT, L_HemisphericLight, light);
    if (!lightProperties(p))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (memcmp(&p->direction, &light->properties.direction, sizeof(p->direction)))
    {
        ++light->dataVersion;
    }
    light->properties = *p;
    return BL_OK;
}

bl_Status bl_setLightIntensity(bl_Light h, double value)
{
    L_GET(h, L_LIGHT, L_HemisphericLight, light);
    if (!isfinite(value))
    {
        return l_error(h._runtime, BL_INVALID_ARGUMENT, __func__, "Nonfinite intensity", 134);
    }
    if (value != light->properties.intensity)
    {
        light->properties.intensity = value;
        ++light->dataVersion;
    }
    return BL_OK;
}

bl_Status bl_markLightUboDirty(bl_Light h)
{
    L_GET(h, L_LIGHT, L_HemisphericLight, light);
    ++light->dataVersion;
    return BL_OK;
}

bl_Status bl_hemisphericLightAsLight(bl_HemisphericLight h, bl_Light* out)
{
    L_GET(h, L_LIGHT, L_HemisphericLight, light);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, light->node.record.id};
    return BL_OK;
}

bl_Status bl_lightNode(bl_Light h, bl_SceneNode* out)
{
    L_GET(h, L_LIGHT, L_Node, light);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, light->record.id};
    return BL_OK;
}

bl_Status l_writeHemisphericLight(bl_Runtime* r, L_HemisphericLight* light, float* data)
{
    L_TRY(l_world(r, &light->node));
    const double* w = light->node.world.values;
    bl_Vec3 direction = light->properties.direction;
    double x = w[0] * direction.x + w[4] * direction.y + w[8] * direction.z;
    double y = w[1] * direction.x + w[5] * direction.y + w[9] * direction.z;
    double z = w[2] * direction.x + w[6] * direction.y + w[10] * direction.z;
    double length = hypot(hypot(x, y), z);
    double inverse = length == 0 ? 1 : 1 / length;
    memset(data, 0, 16 * sizeof(float));
    data[0] = (float)(x * inverse);
    data[1] = (float)(y * inverse);
    data[2] = (float)(z * inverse);
    data[3] = 3;
    double intensity = light->properties.intensity;
    data[4] = (float)(light->properties.diffuseColor.x * intensity);
    data[5] = (float)(light->properties.diffuseColor.y * intensity);
    data[6] = (float)(light->properties.diffuseColor.z * intensity);
    data[8] = (float)(light->properties.specularColor.x * intensity);
    data[9] = (float)(light->properties.specularColor.y * intensity);
    data[10] = (float)(light->properties.specularColor.z * intensity);
    data[12] = (float)(light->properties.groundColor.x * intensity);
    data[13] = (float)(light->properties.groundColor.y * intensity);
    data[14] = (float)(light->properties.groundColor.z * intensity);
    return BL_OK;
}
