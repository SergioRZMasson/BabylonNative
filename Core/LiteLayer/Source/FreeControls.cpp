/* Original camera/free-camera-controls.ts at 2e064d88, Lite 1.32.0, Apache-2.0. */
#include "SceneInternal.h"

struct L_FreeControl
{
    L_Record record;
    L_Node* camera;
    bl_SceneContext scene;
    uint64_t callback;
    bl_Status failure;
    bl_Vec3 direction;
    double pitch;
    double yaw;
    double lastX;
    double lastY;
    uint32_t keys;
    bool dragging;
};

static void cleanup(bl_Runtime* r, L_Record* record)
{
    L_FreeControl* control = (L_FreeControl*)record;
    L_Record* sceneRecord = l_peek(r, control->scene._id);
    if (sceneRecord && !sceneRecord->disposed && sceneRecord->kind == L_SCENE)
    {
        L_Callback** link = &((L_Scene*)sceneRecord)->before;
        while (*link)
        {
            L_Callback* callback = *link;
            if (callback->id == control->callback)
            {
                *link = callback->next;
                l_free(r, callback);
                break;
            }
            link = &callback->next;
        }
    }
    if (control->camera)
    {
        l_unpin(&control->camera->record);
        control->camera = NULL;
    }
    record->disposed = true;
}

static bool key(const L_FreeControl* control, bl_FreeKey first, bl_FreeKey second)
{
    return (control->keys & ((1u << first) | (1u << second))) != 0;
}

static double decay(double value, double inertia, double epsilon)
{
    value *= inertia;
    return fabs(value) < epsilon ? 0 : value;
}

static void update(void* user, double delta)
{
    L_FreeControl* c = (L_FreeControl*)user;
    L_Node* camera = c->camera;
    c->failure = BL_OK;
    double dt = delta > 1 ? delta : 1;
    double speed = camera->camera.speed * sqrt((dt * dt) / 100000);
    bl_Vec3 direction = c->direction;
    if (key(c, BL_FREE_KEY_W, BL_FREE_KEY_ARROW_UP))
    {
        direction.z += speed;
    }
    if (key(c, BL_FREE_KEY_S, BL_FREE_KEY_ARROW_DOWN))
    {
        direction.z -= speed;
    }
    if (key(c, BL_FREE_KEY_A, BL_FREE_KEY_ARROW_LEFT))
    {
        direction.x -= speed;
    }
    if (key(c, BL_FREE_KEY_D, BL_FREE_KEY_ARROW_RIGHT))
    {
        direction.x += speed;
    }
    if (key(c, BL_FREE_KEY_SPACE, BL_FREE_KEY_PAGE_UP))
    {
        direction.y += speed;
    }
    if (key(c, BL_FREE_KEY_SHIFT_LEFT, BL_FREE_KEY_SHIFT_RIGHT) ||
        key(c, BL_FREE_KEY_PAGE_DOWN, BL_FREE_KEY_PAGE_DOWN))
    {
        direction.y -= speed;
    }
    bool moving = direction.x != 0 || direction.y != 0 || direction.z != 0;
    bool rotating = c->pitch != 0 || c->yaw != 0;
    double yaw = camera->freeYaw;
    double pitch = camera->freePitch;
    if (rotating)
    {
        yaw += c->yaw;
        pitch -= c->pitch;
        double maximum = 3.141592653589793 / 2 - .01;
        if (pitch < -maximum)
        {
            pitch = -maximum;
        }
        if (pitch > maximum)
        {
            pitch = maximum;
        }
    }
    bl_Vec3 position = camera->position;
    bl_Vec3 target = camera->target;
    if (moving)
    {
        double cy = cos(yaw);
        double sy = sin(yaw);
        double cp = cos(pitch);
        double sp = sin(pitch);
        position.x += sy * cp * direction.z + cy * direction.x;
        position.y += sp * direction.z + direction.y;
        position.z += cy * cp * direction.z - sy * direction.x;
    }
    if (moving || rotating)
    {
        target = {position.x + sin(yaw) * cos(pitch), position.y + sin(pitch),
                  position.z + cos(yaw) * cos(pitch)};
    }
    if (!l_vec(position) || !l_vec(target) || !isfinite(yaw) || !isfinite(pitch))
    {
        c->failure = L_FAIL(c->scene._runtime, BL_INVALID_ARGUMENT,
                            "Free-control integration produced a nonfinite transform");
        return;
    }
    if (moving || rotating)
    {
        camera->position = position;
        camera->target = target;
        camera->freeYaw = yaw;
        camera->freePitch = pitch;
        camera->localMatrixOverride = false;
        camera->dirty = true;
    }
    double inertia = camera->camera.inertia;
    double epsilon = camera->camera.speed * .001;
    c->direction = {decay(direction.x, inertia, epsilon), decay(direction.y, inertia, epsilon),
                    decay(direction.z, inertia, epsilon)};
    c->pitch = decay(c->pitch, inertia, epsilon);
    c->yaw = decay(c->yaw, inertia, epsilon);
}

bl_Status bl_attachFreeControl(bl_FreeCamera h, bl_SceneContext sceneHandle, bl_FreeControl* out)
{
    L_GET(h, L_CAMERA, L_Node, camera);
    bl_Runtime* r = h._runtime;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (r->dispatchDepth || r->frameDepth)
    {
        return BL_BUSY;
    }
    L_Scene* scene = NULL;
    if (!L_NULL(sceneHandle))
    {
        if (sceneHandle._runtime != r)
        {
            return BL_WRONG_RUNTIME;
        }
        L_Record* record;
        L_TRY(l_get(r, sceneHandle._id, L_SCENE, &record));
        scene = (L_Scene*)record;
    }
    L_NEW(r, L_FREE_CONTROL, 50, cleanup, L_FreeControl, control);
    control->camera = camera;
    control->scene = sceneHandle;
    l_pin(&camera->record);
    if (scene)
    {
        L_Callback* callback = (L_Callback*)l_alloc(r, sizeof(*callback));
        if (!callback)
        {
            cleanup(r, &control->record);
            return BL_OUT_OF_MEMORY;
        }
        if (r->callbackId == UINT64_MAX)
        {
            l_free(r, callback);
            cleanup(r, &control->record);
            return BL_OUT_OF_MEMORY;
        }
        callback->id = ++r->callbackId;
        callback->before = update;
        callback->user = control;
        callback->failure = &control->failure;
        L_Callback** end = &scene->before;
        while (*end)
        {
            end = &(*end)->next;
        }
        *end = callback;
        control->callback = callback->id;
    }
    *out = {r, control->record.id};
    return BL_OK;
}

bl_Status bl_processFreeInput(bl_FreeControl h, const bl_FreeInput* input,
                              bl_ArcRotateInputEffects* out)
{
    L_GET(h, L_FREE_CONTROL, L_FreeControl, control);
    if (!input || !out || (unsigned)input->kind > BL_FREE_KEY_UP ||
        (unsigned)input->key > BL_FREE_KEY_PAGE_DOWN || !isfinite(input->clientX) ||
        !isfinite(input->clientY))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (h._runtime->dispatchDepth || h._runtime->frameDepth)
    {
        return BL_BUSY;
    }
    bl_ArcRotateInputEffects effects = {};
    effects.pointerId = input->pointerId;
    if (input->kind == BL_FREE_POINTER_DOWN && input->button <= 2)
    {
        control->dragging = true;
        control->lastX = input->clientX;
        control->lastY = input->clientY;
        effects.capturePointer = true;
    }
    else if (input->kind == BL_FREE_POINTER_MOVE && control->dragging)
    {
        double sensitivity = control->camera->camera.angularSensitivity;
        double yaw = control->yaw + (input->clientX - control->lastX) / sensitivity;
        double pitch = control->pitch + (input->clientY - control->lastY) / sensitivity;
        if (!isfinite(yaw) || !isfinite(pitch))
        {
            return BL_INVALID_ARGUMENT;
        }
        control->yaw = yaw;
        control->pitch = pitch;
        control->lastX = input->clientX;
        control->lastY = input->clientY;
    }
    else if (input->kind == BL_FREE_POINTER_UP)
    {
        control->dragging = false;
        effects.releasePointer = true;
    }
    else if (input->kind == BL_FREE_CONTEXT_MENU)
    {
        effects.preventDefault = true;
    }
    else if (input->kind == BL_FREE_KEY_DOWN)
    {
        control->keys |= 1u << input->key;
    }
    else if (input->kind == BL_FREE_KEY_UP)
    {
        control->keys &= ~(1u << input->key);
    }
    *out = effects;
    return BL_OK;
}

bl_Status bl_detachFreeControl(bl_FreeControl h)
{
    L_RETIRED(h, L_FREE_CONTROL, L_FreeControl, control);
    if (!control || control->record.disposed)
    {
        return BL_OK;
    }
    if (h._runtime->dispatchDepth || h._runtime->frameDepth)
    {
        return BL_BUSY;
    }
    cleanup(h._runtime, &control->record);
    return BL_OK;
}
