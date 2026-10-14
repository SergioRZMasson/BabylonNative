#include "LiteInternal.h"

bl_Status bl_createDefaultCamera(bl_SceneContext handle, bl_ArcRotateCamera* out)
{
    L_GET(handle, L_SCENE, L_Scene, scene);
    bl_Runtime* runtime = handle._runtime;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_Vec3 minimum = {INFINITY, INFINITY, INFINITY};
    bl_Vec3 maximum = {-INFINITY, -INFINITY, -INFINITY};
    bool hasBounds = false;
    for (size_t i = 0; i < scene->memberCount; ++i)
    {
        L_Node* node = scene->members[i];
        if (node->record.kind == L_MESH && !node->record.disposed && node->visible)
        {
            L_TRY(l_meshWorldBounds(runtime, (L_Mesh*)node, &minimum, &maximum, &hasBounds));
        }
    }
    double x = maximum.x - minimum.x;
    double y = maximum.y - minimum.y;
    double z = maximum.z - minimum.z;
    double radius = sqrt(x * x + y * y + z * z) * 1.5;
    bl_Vec3 center = {minimum.x + x * 0.5, minimum.y + y * 0.5, minimum.z + z * 0.5};
    if (!hasBounds || !isfinite(radius) || radius == 0)
    {
        radius = 1;
        center = {};
    }
    bl_ArcRotateCamera camera = {};
    L_TRY(bl_createArcRotateCamera(runtime, -3.141592653589793 / 2, 3.141592653589793 / 2, radius,
                                   center, &camera));
    bl_ArcRotateCameraProperties properties;
    bl_Status status = bl_getArcRotateCameraProperties(camera, &properties);
    if (status == BL_OK)
    {
        properties.nearPlane = radius * 0.01;
        properties.farPlane = radius * 1000;
        status = bl_setArcRotateCameraProperties(camera, &properties);
    }
    bl_Camera generic = {};
    if (status == BL_OK)
    {
        status = bl_arcRotateCameraAsCamera(camera, &generic);
    }
    bl_SceneProperties2 state;
    if (status == BL_OK)
    {
        status = bl_getSceneProperties2(handle, &state);
    }
    if (status == BL_OK)
    {
        state.camera = generic;
        status = bl_setSceneProperties2(handle, &state);
    }
    if (status != BL_OK)
    {
        bl_disposeNode({runtime, camera._id});
        return status;
    }
    *out = camera;
    return BL_OK;
}
