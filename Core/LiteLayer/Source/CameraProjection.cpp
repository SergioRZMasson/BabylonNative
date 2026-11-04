#include "LiteInternal.h"

static bool validBounds(const bl_OrthographicBounds* b)
{
    if (!b || !isfinite(b->halfHeight) || b->halfHeight == 0)
    {
        return false;
    }
    const bl_OptionalNumber planes[4] = {b->left, b->right, b->bottom, b->top};
    for (unsigned i = 0; i < 4; ++i)
    {
        if (planes[i].present && !isfinite(planes[i].value))
        {
            return false;
        }
    }
    return !(b->left.present && b->right.present && b->left.value == b->right.value) &&
           !(b->bottom.present && b->top.present && b->bottom.value == b->top.value);
}

bl_Status bl_enableOrthographicCamera(bl_Camera h, const bl_OrthographicBounds* options)
{
    L_Node* camera;
    L_TRY(l_cameraFamily(h, &camera));
    bl_OrthographicBounds bounds = {1, {}, {}, {}, {}};
    if (options)
    {
        bounds = *options;
    }
    if (!validBounds(&bounds))
    {
        return BL_INVALID_ARGUMENT;
    }
    camera->ortho = bounds;
    camera->orthographic = true;
    return BL_OK;
}

bl_Status bl_getOrthographicBounds(bl_Camera h, bl_OrthographicBounds* out)
{
    L_Node* camera;
    L_TRY(l_cameraFamily(h, &camera));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!camera->orthographic)
    {
        return BL_NOT_READY;
    }
    *out = camera->ortho;
    return BL_OK;
}

bl_Status bl_setOrthographicBounds(bl_Camera h, const bl_OrthographicBounds* bounds)
{
    L_Node* camera;
    L_TRY(l_cameraFamily(h, &camera));
    if (!camera->orthographic)
    {
        return BL_NOT_READY;
    }
    if (!validBounds(bounds))
    {
        return BL_INVALID_ARGUMENT;
    }
    camera->ortho = *bounds;
    return BL_OK;
}

bl_Status bl_disableOrthographicCamera(bl_Camera h)
{
    L_Node* camera;
    L_TRY(l_cameraFamily(h, &camera));
    camera->orthographic = false;
    return BL_OK;
}

bl_Status bl_enableFloatingOrigin(bl_EngineContext h)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    if (!engine->highPrecision)
    {
        return l_error(h._runtime, BL_INVALID_ARGUMENT, __func__,
                       "Floating origin requires high-precision matrices", 48);
    }
    if (engine->floatingOrigin)
    {
        return BL_OK;
    }
    if (engine->inFrame || engine->scenes)
    {
        return BL_BUSY;
    }
    for (size_t i = 0; i < h._runtime->count; ++i)
    {
        L_Record* record = h._runtime->records[i];
        if (record && !record->disposed && record->kind == L_SCENE &&
            ((L_Scene*)record)->engine == engine)
        {
            return BL_BUSY;
        }
    }
    engine->floatingOrigin = true;
    return BL_OK;
}

bl_Status bl_getMsaaSamples(bl_EngineContext h, uint32_t* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = engine->native.target.sampleCount;
    return BL_OK;
}

void l_renderWorld(const L_Scene* scene, const L_Node* node, bl_Mat4* world)
{
    *world = node->world;
    if (scene->engine->floatingOrigin)
    {
        world->values[12] -= scene->origin.x;
        world->values[13] -= scene->origin.y;
        world->values[14] -= scene->origin.z;
    }
}

bl_Status bl_setAlphaToCoverage(bl_Material h, bool enabled)
{
    L_Material* material;
    L_TRY(l_materialFamily(h, &material));
    material->alphaToCoverage = enabled;
    return BL_OK;
}

bl_Status bl_getAlphaToCoverage(bl_Material h, bool* out)
{
    L_Material* material;
    L_TRY(l_materialFamily(h, &material));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = material->alphaToCoverage;
    return BL_OK;
}

bl_Status bl_enableMirroredMeshes(bl_SceneContext h)
{
    L_GET(h, L_SCENE, L_Scene, scene);
    h._runtime->mirroredMeshes = true;
    scene->trackMirrors = true;
    return BL_OK;
}
