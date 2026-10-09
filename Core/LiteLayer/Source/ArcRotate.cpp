#include "ArcRotateInternal.h"

bool l_isNodeKind(unsigned kind)
{
    return kind == L_NODE || kind == L_CAMERA || kind == L_MESH || kind == L_ARC_CAMERA ||
           kind == L_LIGHT;
}

bl_Status l_cameraFamily(bl_Camera h, L_Node** out)
{
    L_Record* record;
    L_TRY(l_get(h._runtime, h._id, 0, &record));
    if (record->kind != L_CAMERA && record->kind != L_ARC_CAMERA)
    {
        return BL_INVALID_HANDLE;
    }
    *out = (L_Node*)record;
    return BL_OK;
}

static void cameraCleanup(bl_Runtime* r, L_Record* record)
{
    l_cleanNode(r, (L_Node*)record);
}

bool l_arcProperties(const bl_ArcRotateCameraProperties* p)
{
    return p && isfinite(p->alpha) && isfinite(p->beta) && isfinite(p->radius) &&
           l_vec(p->target) && isfinite(p->fov) && p->fov > 0 && p->fov < 3.141592653589793 &&
           isfinite(p->nearPlane) && p->nearPlane > 0 && isfinite(p->farPlane) &&
           p->farPlane > p->nearPlane && isfinite(p->inertia) && isfinite(p->panningInertia) &&
           isfinite(p->angularSensibility) && p->angularSensibility > 0 &&
           isfinite(p->panningSensibility) && p->panningSensibility > 0 &&
           isfinite(p->wheelPrecision) && p->wheelPrecision > 0 &&
           isfinite(p->inertialAlphaOffset) && isfinite(p->inertialBetaOffset) &&
           isfinite(p->inertialRadiusOffset) && isfinite(p->inertialPanningX) &&
           isfinite(p->inertialPanningY);
}

static void clampValue(double* value, double* offset, bl_OptionalNumber lower,
                       bl_OptionalNumber upper)
{
    if (lower.present && *value < lower.value)
    {
        *value = lower.value;
        *offset = 0;
    }
    else if (upper.present && *value > upper.value)
    {
        *value = upper.value;
        *offset = 0;
    }
}

void l_arcClamp(L_ArcCamera* c)
{
    if (!c->limitOwner)
    {
        return;
    }
    bl_ArcRotateCameraProperties* p = &c->properties;
    clampValue(&p->radius, &p->inertialRadiusOffset, c->limits.lowerRadiusLimit,
               c->limits.upperRadiusLimit);
    clampValue(&p->beta, &p->inertialBetaOffset, c->limits.lowerBetaLimit,
               c->limits.upperBetaLimit);
    clampValue(&p->alpha, &p->inertialAlphaOffset, c->limits.lowerAlphaLimit,
               c->limits.upperAlphaLimit);
}

void l_arcCommit(L_ArcCamera* c, const bl_ArcRotateCameraProperties* p)
{
    bl_ArcRotateCameraProperties old = c->properties;
    bl_ArcRotateCameraProperties incoming = *p;
    c->properties = incoming;
    c->properties.alpha = old.alpha;
    c->properties.beta = old.beta;
    c->properties.radius = old.radius;
    if (old.alpha != incoming.alpha && c->properties.alpha != incoming.alpha)
    {
        c->properties.alpha = incoming.alpha;
        c->node.dirty = true;
        l_arcClamp(c);
    }
    if (old.beta != incoming.beta && c->properties.beta != incoming.beta)
    {
        c->properties.beta = incoming.beta;
        c->node.dirty = true;
        l_arcClamp(c);
    }
    if (old.radius != incoming.radius && c->properties.radius != incoming.radius)
    {
        c->properties.radius = incoming.radius;
        c->node.dirty = true;
        l_arcClamp(c);
    }
    if (old.alpha != c->properties.alpha || old.beta != c->properties.beta ||
        old.radius != c->properties.radius ||
        memcmp(&old.target, &c->properties.target, sizeof(old.target)))
    {
        c->node.dirty = true;
    }
    c->node.camera.fov = incoming.fov;
    c->node.camera.nearPlane = incoming.nearPlane;
    c->node.camera.farPlane = incoming.farPlane;
}

void l_arcEye(L_Node* n, bl_Vec3* eye)
{
    L_ArcCamera* c = (L_ArcCamera*)n;
    const bl_ArcRotateCameraProperties* p = &c->properties;
    double sinBeta = sin(p->beta);
    if (sinBeta == 0)
    {
        sinBeta = .0001;
    }
    *eye = {p->target.x + p->radius * cos(p->alpha) * sinBeta,
            p->target.y + p->radius * cos(p->beta),
            p->target.z + p->radius * sin(p->alpha) * sinBeta};
}

void l_arcView(const L_Node* n, bl_Mat4* out)
{
    const double* w = n->world.values;
    double* v = out->values;
    l_identity(out);
    for (unsigned column = 0; column < 3; ++column)
    {
        for (unsigned row = 0; row < 3; ++row)
        {
            v[column * 4 + row] = w[row * 4 + column];
        }
        double value =
            -(w[column * 4] * w[12] + w[column * 4 + 1] * w[13] + w[column * 4 + 2] * w[14]);
        v[12 + column] = n->highPrecision ? value : (double)(float)value;
    }
}

bl_Status bl_createArcRotateCamera(bl_Runtime* r, double alpha, double beta, double radius,
                                   bl_Vec3 target, bl_ArcRotateCamera* out)
{
    L_TRY(l_check(r));
    if (!out || !isfinite(alpha) || !isfinite(beta) || !isfinite(radius) || !l_vec(target))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_NEW(r, L_ARC_CAMERA, 30, cameraCleanup, L_ArcCamera, c);
    l_initNode(&c->node);
    c->properties = {alpha, beta, radius, target, .8, .1, 1000, .9, .9, 1000, 50, 3, 0, 0, 0, 0, 0};
    l_arcCommit(c, &c->properties);
    *out = {r, c->node.record.id};
    return BL_OK;
}

bl_Status bl_getArcRotateCameraProperties(bl_ArcRotateCamera h, bl_ArcRotateCameraProperties* out)
{
    L_GET(h, L_ARC_CAMERA, L_ArcCamera, c);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = c->properties;
    return BL_OK;
}

bl_Status bl_setArcRotateCameraProperties(bl_ArcRotateCamera h,
                                          const bl_ArcRotateCameraProperties* p)
{
    L_GET(h, L_ARC_CAMERA, L_ArcCamera, c);
    if (!l_arcProperties(p))
    {
        return BL_INVALID_ARGUMENT;
    }
    l_arcCommit(c, p);
    return BL_OK;
}

bl_Status bl_freeCameraAsCamera(bl_FreeCamera h, bl_Camera* out)
{
    L_GET(h, L_CAMERA, L_Node, c);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, c->record.id};
    return BL_OK;
}

bl_Status bl_arcRotateCameraAsCamera(bl_ArcRotateCamera h, bl_Camera* out)
{
    L_GET(h, L_ARC_CAMERA, L_Node, c);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, c->record.id};
    return BL_OK;
}

bl_Status bl_cameraAsFreeCamera(bl_Camera h, bl_FreeCamera* out)
{
    L_GET(h, L_CAMERA, L_Node, c);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, c->record.id};
    return BL_OK;
}

bl_Status bl_cameraAsArcRotateCamera(bl_Camera h, bl_ArcRotateCamera* out)
{
    L_GET(h, L_ARC_CAMERA, L_Node, c);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, c->record.id};
    return BL_OK;
}

bl_Status bl_arcRotateCameraNode(bl_ArcRotateCamera h, bl_SceneNode* out)
{
    L_GET(h, L_ARC_CAMERA, L_Node, c);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, c->record.id};
    return BL_OK;
}

static void limitCleanup(bl_Runtime*, L_Record* record)
{
    L_Limit* token = (L_Limit*)record;
    if (token->camera->limitOwner == record->id)
    {
        token->camera->limitOwner = 0;
    }
    l_unpin(&token->camera->node.record);
}

bl_Status bl_getArcRotateCameraLimits(bl_ArcRotateCamera h, bl_ArcRotateCameraLimits* out,
                                      bool* enforced)
{
    L_GET(h, L_ARC_CAMERA, L_ArcCamera, c);
    if (!out || !enforced)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = c->limits;
    *enforced = c->limitOwner != 0;
    return BL_OK;
}

static bool validBounds(bl_OptionalNumber lower, bl_OptionalNumber upper)
{
    return (!lower.present || isfinite(lower.value)) && (!upper.present || isfinite(upper.value)) &&
           (!lower.present || !upper.present || lower.value <= upper.value);
}

static bl_Status mergedLimits(L_ArcCamera* c, const bl_ArcRotateCameraLimitPatch* patch,
                              bl_ArcRotateCameraLimits* out)
{
    if (!patch || (patch->fields & ~UINT32_C(63)))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_ArcRotateCameraLimits limits = c->limits;
    if (patch->fields & BL_LIMIT_LOWER_ALPHA)
    {
        limits.lowerAlphaLimit = patch->values.lowerAlphaLimit;
    }
    if (patch->fields & BL_LIMIT_UPPER_ALPHA)
    {
        limits.upperAlphaLimit = patch->values.upperAlphaLimit;
    }
    if (patch->fields & BL_LIMIT_LOWER_BETA)
    {
        limits.lowerBetaLimit = patch->values.lowerBetaLimit;
    }
    if (patch->fields & BL_LIMIT_UPPER_BETA)
    {
        limits.upperBetaLimit = patch->values.upperBetaLimit;
    }
    if (patch->fields & BL_LIMIT_LOWER_RADIUS)
    {
        limits.lowerRadiusLimit = patch->values.lowerRadiusLimit;
    }
    if (patch->fields & BL_LIMIT_UPPER_RADIUS)
    {
        limits.upperRadiusLimit = patch->values.upperRadiusLimit;
    }
    if (!validBounds(limits.lowerAlphaLimit, limits.upperAlphaLimit) ||
        !validBounds(limits.lowerBetaLimit, limits.upperBetaLimit) ||
        !validBounds(limits.lowerRadiusLimit, limits.upperRadiusLimit))
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = limits;
    return BL_OK;
}

bl_Status bl_setArcRotateCameraLimitFields(bl_ArcRotateCamera h,
                                           const bl_ArcRotateCameraLimitPatch* patch)
{
    L_GET(h, L_ARC_CAMERA, L_ArcCamera, c);
    bl_ArcRotateCameraLimits limits;
    L_TRY(mergedLimits(c, patch, &limits));
    c->limits = limits;
    return BL_OK;
}

bl_Status bl_setCameraLimits(bl_ArcRotateCamera h, const bl_ArcRotateCameraLimitPatch* patch,
                             bl_CameraLimitToken* out)
{
    L_GET(h, L_ARC_CAMERA, L_ArcCamera, c);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_ArcRotateCameraLimits limits;
    L_TRY(mergedLimits(c, patch, &limits));
    L_NEW(h._runtime, L_LIMIT, 55, limitCleanup, L_Limit, token);
    token->camera = c;
    l_pin(&c->node.record);
    c->limits = limits;
    c->limitOwner = token->record.id;
    bl_ArcRotateCameraProperties before = c->properties;
    l_arcClamp(c);
    if (before.alpha != c->properties.alpha || before.beta != c->properties.beta ||
        before.radius != c->properties.radius)
    {
        c->node.dirty = true;
    }
    *out = {h._runtime, token->record.id};
    return BL_OK;
}

bl_Status bl_removeCameraLimits(bl_CameraLimitToken h)
{
    L_RETIRED(h, L_LIMIT, L_Limit, token);
    if (token && !token->record.disposed)
    {
        limitCleanup(h._runtime, &token->record);
        token->record.disposed = true;
    }
    return BL_OK;
}
