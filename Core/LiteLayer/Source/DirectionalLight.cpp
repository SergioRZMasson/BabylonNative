#include "DirectionalLightInternal.h"

static void directionalCleanup(bl_Runtime* r, L_Record* record)
{
    l_cleanNode(r, (L_Node*)record);
}

bl_Status l_directionalLight(bl_DirectionalLight h, L_DirectionalLight** out)
{
    L_Light* light;
    L_TRY(l_lightFamily({h._runtime, h._id}, &light));
    if (light->kind != L_LIGHT_DIRECTIONAL)
    {
        return BL_INVALID_HANDLE;
    }
    *out = (L_DirectionalLight*)light;
    return BL_OK;
}

bl_Status bl_createDirectionalLight(bl_Runtime* r, const bl_DirectionalLightOptions* options,
                                    bl_DirectionalLight* out)
{
    L_TRY(l_check(r));
    if (!options || !out || !l_vec(options->direction) ||
        (options->intensity.present && !isfinite(options->intensity.value)))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_NEW(r, L_LIGHT, 30, directionalCleanup, L_DirectionalLight, light);
    l_initNode(&light->light.node);
    light->light.kind = L_LIGHT_DIRECTIONAL;
    light->properties = {options->direction,
                         options->intensity.present ? options->intensity.value : 1,
                         {1, 1, 1},
                         {1, 1, 1}};
    *out = {r, light->light.node.record.id};
    return BL_OK;
}

bl_Status bl_getDirectionalLightProperties(bl_DirectionalLight h,
                                           bl_DirectionalLightProperties* out)
{
    L_DirectionalLight* light;
    L_TRY(l_directionalLight(h, &light));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = light->properties;
    return BL_OK;
}

bl_Status bl_setDirectionalLightProperties(bl_DirectionalLight h,
                                           const bl_DirectionalLightProperties* p)
{
    L_DirectionalLight* light;
    L_TRY(l_directionalLight(h, &light));
    if (!p || !l_vec(p->direction) || !isfinite(p->intensity) || !l_vec(p->diffuse) ||
        !l_vec(p->specular))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (memcmp(&p->direction, &light->properties.direction, sizeof(p->direction)))
    {
        ++light->light.dataVersion;
    }
    light->properties = *p;
    return BL_OK;
}

bl_Status bl_directionalLightAsLight(bl_DirectionalLight h, bl_Light* out)
{
    L_DirectionalLight* light;
    L_TRY(l_directionalLight(h, &light));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, light->light.node.record.id};
    return BL_OK;
}

bl_Status bl_lightAsDirectionalLight(bl_Light h, bl_DirectionalLight* out)
{
    L_DirectionalLight* light;
    L_TRY(l_directionalLight({h._runtime, h._id}, &light));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, light->light.node.record.id};
    return BL_OK;
}

bl_Status l_writeDirectionalLight(bl_Runtime* r, L_DirectionalLight* light, float* data)
{
    L_TRY(l_world(r, &light->light.node));
    const double* w = light->light.node.world.values;
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
    data[3] = 1;
    double intensity = light->properties.intensity;
    data[4] = (float)(light->properties.diffuse.x * intensity);
    data[5] = (float)(light->properties.diffuse.y * intensity);
    data[6] = (float)(light->properties.diffuse.z * intensity);
    data[7] = INFINITY;
    data[8] = (float)(light->properties.specular.x * intensity);
    data[9] = (float)(light->properties.specular.y * intensity);
    data[10] = (float)(light->properties.specular.z * intensity);
    return BL_OK;
}
