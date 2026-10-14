#ifndef LITE_SANDBOX_SCENE_CAMERA_H
#define LITE_SANDBOX_SCENE_CAMERA_H

#include <babylon_lite.h>

// Host policy for a selected scene without primitives; not a DefaultCamera API change.
static bl_Status sandboxSceneCamera(bl_SceneContext scene, bool empty, bl_ArcRotateCamera* out)
{
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!empty)
    {
        return bl_createDefaultCamera(scene, out);
    }
    bl_ArcRotateCamera camera = {};
    bl_Status status = bl_createArcRotateCamera(scene._runtime, -1.5707963267948966,
                                                1.5707963267948966, 1, {0, 0, 0}, &camera);
    bl_ArcRotateCameraProperties properties = {};
    if (status == BL_OK)
    {
        status = bl_getArcRotateCameraProperties(camera, &properties);
    }
    if (status == BL_OK)
    {
        properties.nearPlane = 0.01;
        properties.farPlane = 1000;
        status = bl_setArcRotateCameraProperties(camera, &properties);
    }
    bl_Camera generic = {};
    if (status == BL_OK)
    {
        status = bl_arcRotateCameraAsCamera(camera, &generic);
    }
    bl_SceneProperties2 state = {};
    if (status == BL_OK)
    {
        status = bl_getSceneProperties2(scene, &state);
    }
    if (status == BL_OK)
    {
        state.camera = generic;
        status = bl_setSceneProperties2(scene, &state);
    }
    if (status != BL_OK)
    {
        if (camera._id)
        {
            bl_disposeNode({scene._runtime, camera._id});
        }
        return status;
    }
    *out = camera;
    return BL_OK;
}

#endif
