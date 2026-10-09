#include "ArcRotateInternal.h"

static void controlCleanup(bl_Runtime* r, L_Record* record)
{
    L_Control* c = (L_Control*)record;
    if (c->hook)
    {
        L_Callback** link = &c->scene->before;
        while (*link && *link != c->hook)
        {
            link = &(*link)->next;
        }
        if (*link)
        {
            *link = c->hook->next;
        }
        l_free(r, c->hook);
        c->hook = NULL;
    }
    if (c->scene)
    {
        l_unpin(&c->scene->record);
    }
    l_unpin(&c->camera->node.record);
    l_free(r, c->touches);
    l_free(r, c->touchScratch);
    c->touches = NULL;
    c->touchScratch = NULL;
}

static void clampOrbit(L_ArcCamera* c, bl_ArcRotateCameraProperties* p)
{
    if (!c->limitOwner)
    {
        return;
    }
    L_ArcCamera temporary = {};
    temporary.limitOwner = c->limitOwner;
    temporary.limits = c->limits;
    temporary.properties = *p;
    l_arcClamp(&temporary);
    *p = temporary.properties;
}

static double decay(double value, double inertia, double epsilon)
{
    value *= inertia;
    return fabs(value) < epsilon ? 0 : value;
}

static bool orbitWrite(L_ArcCamera* c, bl_ArcRotateCameraProperties* p, double* member,
                       double value)
{
    if (*member != value)
    {
        *member = value;
        clampOrbit(c, p);
        return true;
    }
    return false;
}

static void applyInertia(void* user, double)
{
    L_Control* c = (L_Control*)user;
    bl_ArcRotateCameraProperties p = c->camera->properties;
    bool written = false;
    if (p.inertialAlphaOffset != 0 || p.inertialBetaOffset != 0)
    {
        written |= orbitWrite(c->camera, &p, &p.alpha, p.alpha + p.inertialAlphaOffset);
        written |= orbitWrite(c->camera, &p, &p.beta, p.beta + p.inertialBetaOffset);
        written |=
            orbitWrite(c->camera, &p, &p.beta, fmax(.01, fmin(3.141592653589793 - .01, p.beta)));
        p.inertialAlphaOffset = decay(p.inertialAlphaOffset, p.inertia, .001);
        p.inertialBetaOffset = decay(p.inertialBetaOffset, p.inertia, .001);
    }
    if (p.inertialRadiusOffset != 0)
    {
        written |= orbitWrite(c->camera, &p, &p.radius, p.radius - p.inertialRadiusOffset);
        written |= orbitWrite(c->camera, &p, &p.radius, fmax(.01, p.radius));
        p.inertialRadiusOffset = decay(p.inertialRadiusOffset, p.inertia, .001);
    }
    if (p.inertialPanningX != 0 || p.inertialPanningY != 0)
    {
        double scale = p.radius * .001;
        p.target.x += -sin(p.alpha) * p.inertialPanningX * scale;
        p.target.y += p.inertialPanningY * scale;
        p.target.z += cos(p.alpha) * p.inertialPanningX * scale;
        p.inertialPanningX = decay(p.inertialPanningX, p.panningInertia, .0001);
        p.inertialPanningY = decay(p.inertialPanningY, p.panningInertia, .0001);
    }
    if (!l_arcProperties(&p))
    {
        c->failure = l_error(c->runtime, BL_INVALID_ARGUMENT, "attachControl",
                             "Inertial arithmetic overflow");
        return;
    }
    l_arcCommit(c->camera, &p);
    if (written)
    {
        c->camera->node.dirty = true;
    }
}

bl_Status bl_attachControl(bl_ArcRotateCamera h, bl_SceneContext scene,
                           const bl_ArcRotateControlOptions* options, bl_ArcRotateControl* out)
{
    L_GET(h, L_ARC_CAMERA, L_ArcCamera, camera);
    bl_Runtime* r = h._runtime;
    if (l_inDispatch())
    {
        return BL_BUSY;
    }
    if (!out || (options && ((unsigned)options->primaryButton > BL_ARC_ACTION_PAN ||
                             (unsigned)options->secondaryButton > BL_ARC_ACTION_PAN)))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (options && options->keyboard)
    {
        return BL_UNSUPPORTED;
    }
    L_Scene* s = NULL;
    L_Callback* hook = NULL;
    if (!L_NULL(scene))
    {
        if (scene._runtime != r)
        {
            return BL_WRONG_RUNTIME;
        }
        L_Record* record;
        L_TRY(l_get(r, scene._id, L_SCENE, &record));
        s = (L_Scene*)record;
        hook = (L_Callback*)l_alloc(r, sizeof(*hook));
        if (!hook)
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    L_Record* record;
    bl_Status status = l_record(r, sizeof(L_Control), L_CONTROL, 55, controlCleanup, &record);
    if (status != BL_OK)
    {
        l_free(r, hook);
        return status;
    }
    L_Control* c = (L_Control*)record;
    c->runtime = r;
    c->camera = camera;
    c->scene = s;
    c->hook = hook;
    if (options)
    {
        c->options = *options;
    }
    l_pin(&camera->node.record);
    if (s)
    {
        l_pin(&s->record);
        hook->before = applyInertia;
        hook->user = c;
        hook->control = c;
        hook->id = ++r->callbackId;
        L_Callback** link = &s->before;
        while (*link)
        {
            link = &(*link)->next;
        }
        *link = hook;
    }
    *out = {r, record->id};
    return BL_OK;
}

bl_Status bl_detachControl(bl_ArcRotateControl h)
{
    L_RETIRED(h, L_CONTROL, L_Control, c);
    if ((l_inDispatch() && !(c && c->scene && c->scene->disposing)) || (c && c->inInput))
    {
        return BL_BUSY;
    }
    if (c && !c->record.disposed)
    {
        controlCleanup(h._runtime, &c->record);
        c->record.disposed = true;
    }
    return BL_OK;
}

bl_Status bl_setArcRotateControlOptions(bl_ArcRotateControl h,
                                        const bl_ArcRotateControlOptions* options)
{
    L_GET(h, L_CONTROL, L_Control, c);
    if (c->inInput)
    {
        return BL_BUSY;
    }
    if (!options || (unsigned)options->primaryButton > BL_ARC_ACTION_PAN ||
        (unsigned)options->secondaryButton > BL_ARC_ACTION_PAN)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (options->keyboard)
    {
        return BL_UNSUPPORTED;
    }
    c->options = *options;
    return BL_OK;
}

static bl_Status reserveTouches(bl_Runtime* r, L_Control* c, size_t need)
{
    if (need <= c->touchCapacity)
    {
        return BL_OK;
    }
    size_t capacity = c->touchCapacity ? c->touchCapacity : 4;
    while (capacity < need)
    {
        if (capacity > SIZE_MAX / 2)
        {
            capacity = need;
            break;
        }
        capacity *= 2;
    }
    size_t bytes;
    if (!l_size(capacity, sizeof(bl_ArcRotateTouch), &bytes))
    {
        return BL_OUT_OF_MEMORY;
    }
    bl_ArcRotateTouch* touches = (bl_ArcRotateTouch*)l_alloc(r, bytes);
    bl_ArcRotateTouch* scratch = (bl_ArcRotateTouch*)l_alloc(r, bytes);
    if (!touches || !scratch)
    {
        l_free(r, touches);
        l_free(r, scratch);
        return BL_OUT_OF_MEMORY;
    }
    if (c->touchCount)
    {
        memcpy(touches, c->touches, c->touchCount * sizeof(*touches));
    }
    l_free(r, c->touches);
    l_free(r, c->touchScratch);
    c->touches = touches;
    c->touchScratch = scratch;
    c->touchCapacity = capacity;
    return BL_OK;
}

static bool statePredicate(bl_Runtime* r, bl_ArcRotateStatePredicate predicate, void* user)
{
    bool result = false;
    if (predicate)
    {
        l_enterHost(r);
        result = predicate(user);
        l_leaveHost(r);
    }
    return result;
}

static double touchDistance(const L_Control* c)
{
    return hypot(c->touches[1].clientX - c->touches[0].clientX,
                 c->touches[1].clientY - c->touches[0].clientY);
}

static bl_Status inputWork(L_Control* c, const bl_ArcRotateInput* i,
                           bl_ArcRotateCameraProperties* p, bl_ArcRotateInputEffects* effects)
{
    if (i->kind == BL_ARC_POINTER_DOWN)
    {
        bool accept = true;
        if (c->options.shouldHandlePointerDown)
        {
            l_enterHost(c->runtime);
            accept = c->options.shouldHandlePointerDown(c->options.userData, i);
            l_leaveHost(c->runtime);
        }
        if (!accept)
        {
            return BL_OK;
        }
        effects->capturePointer = true;
        effects->pointerId = i->pointerId;
        c->lastX = i->clientX;
        c->lastY = i->clientY;
        bl_ArcRotatePointerAction action = BL_ARC_ACTION_DEFAULT;
        if (i->button == 0)
        {
            action = i->pointerType == BL_ARC_POINTER_TOUCH ? BL_ARC_ACTION_ROTATE
                                                            : c->options.primaryButton;
            if (action == BL_ARC_ACTION_DEFAULT)
            {
                action = BL_ARC_ACTION_ROTATE;
            }
        }
        else if (i->button == 2)
        {
            action = c->options.secondaryButton;
            if (action == BL_ARC_ACTION_DEFAULT)
            {
                action = BL_ARC_ACTION_PAN;
            }
        }
        if (action != BL_ARC_ACTION_DEFAULT)
        {
            c->dragging = action == BL_ARC_ACTION_ROTATE;
            c->panning = !c->dragging;
        }
    }
    else if (i->kind == BL_ARC_POINTER_MOVE)
    {
        double dx = i->clientX - c->lastX;
        double dy = i->clientY - c->lastY;
        if (!isfinite(dx) || !isfinite(dy))
        {
            return BL_INVALID_ARGUMENT;
        }
        c->lastX = i->clientX;
        c->lastY = i->clientY;
        if (c->touchCount >= 2 || (!c->dragging && !c->panning))
        {
            return BL_OK;
        }
        if (statePredicate(c->runtime, c->options.isExternalDragActive, c->options.userData))
        {
            c->dragging = false;
            c->panning = false;
            p->inertialAlphaOffset = 0;
            p->inertialBetaOffset = 0;
            p->inertialPanningX = 0;
            p->inertialPanningY = 0;
            return BL_OK;
        }
        if (statePredicate(c->runtime, c->options.isExternalPickPending, c->options.userData))
        {
            return BL_OK;
        }
        if (c->dragging)
        {
            p->inertialAlphaOffset -= dx / p->angularSensibility;
            p->inertialBetaOffset -= dy / p->angularSensibility;
        }
        if (c->panning)
        {
            p->inertialPanningX -= dx / p->panningSensibility;
            p->inertialPanningY += dy / p->panningSensibility;
        }
    }
    else if (i->kind == BL_ARC_POINTER_UP)
    {
        effects->releasePointer = true;
        effects->pointerId = i->pointerId;
        c->dragging = false;
        c->panning = false;
    }
    else if (i->kind == BL_ARC_WHEEL)
    {
        effects->preventDefault = true;
        p->inertialRadiusOffset -= i->deltaY * p->radius / (p->wheelPrecision * 1000);
    }
    else if (i->kind == BL_ARC_CONTEXT_MENU || i->kind == BL_ARC_GESTURE)
    {
        effects->preventDefault = true;
    }
    else
    {
        for (size_t j = 0; j < i->changedTouchCount; ++j)
        {
            const bl_ArcRotateTouch* touch = i->changedTouches + j;
            size_t index = 0;
            while (index < c->touchCount && c->touches[index].identifier != touch->identifier)
            {
                ++index;
            }
            if (i->kind == BL_ARC_TOUCH_END)
            {
                if (index < c->touchCount)
                {
                    memmove(c->touches + index, c->touches + index + 1,
                            (c->touchCount - index - 1) * sizeof(*touch));
                    --c->touchCount;
                }
            }
            else
            {
                c->touches[index] = *touch;
                if (index == c->touchCount)
                {
                    ++c->touchCount;
                }
            }
        }
        if (i->kind == BL_ARC_TOUCH_END)
        {
            if (c->touchCount == 1)
            {
                c->lastX = c->touches[0].clientX;
                c->lastY = c->touches[0].clientY;
            }
            if (c->touchCount < 2)
            {
                c->pinchDistance = 0;
            }
        }
        else if (c->touchCount >= 2)
        {
            effects->preventDefault = true;
            double distance = touchDistance(c);
            if (!isfinite(distance))
            {
                return BL_INVALID_ARGUMENT;
            }
            if (i->kind == BL_ARC_TOUCH_START)
            {
                c->dragging = false;
                c->panning = false;
                c->pinchDistance = distance;
                c->pinchRadius = p->radius;
            }
            else if (distance > 0 && c->pinchDistance > 0)
            {
                c->poseWritten |= orbitWrite(c->camera, p, &p->radius,
                                             c->pinchRadius * (c->pinchDistance / distance));
                c->poseWritten |= orbitWrite(c->camera, p, &p->radius, fmax(.01, p->radius));
            }
        }
    }
    return l_arcProperties(p) ? BL_OK : BL_INVALID_ARGUMENT;
}

bl_Status bl_processArcRotateInput(bl_ArcRotateControl h, const bl_ArcRotateInput* i,
                                   bl_ArcRotateInputEffects* out)
{
    L_GET(h, L_CONTROL, L_Control, c);
    if (c->inInput || l_inDispatch())
    {
        return BL_BUSY;
    }
    if (!i || !out || (unsigned)i->kind > BL_ARC_GESTURE ||
        (unsigned)i->pointerType > BL_ARC_POINTER_TOUCH || !isfinite(i->clientX) ||
        !isfinite(i->clientY) || !isfinite(i->deltaY) ||
        !l_typedSpan(i->changedTouches, i->changedTouchCount, alignof(bl_ArcRotateTouch)) ||
        i->changedTouchCount > SIZE_MAX - c->touchCount)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t j = 0; j < i->changedTouchCount; ++j)
    {
        if (!isfinite(i->changedTouches[j].clientX) || !isfinite(i->changedTouches[j].clientY))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    L_TRY(reserveTouches(h._runtime, c, c->touchCount + i->changedTouchCount));
    L_Control work = *c;
    work.poseWritten = false;
    work.touches = c->touchScratch;
    if (c->touchCount)
    {
        memcpy(work.touches, c->touches, c->touchCount * sizeof(*c->touches));
    }
    bl_ArcRotateCameraProperties properties = c->camera->properties;
    bl_ArcRotateInputEffects effects = {};
    c->inInput = true;
    bl_Status status = inputWork(&work, i, &properties, &effects);
    c->inInput = false;
    if (status != BL_OK)
    {
        return status;
    }
    work.inInput = false;
    work.touches = c->touches;
    *c = work;
    if (c->touchCount)
    {
        memcpy(c->touches, c->touchScratch, c->touchCount * sizeof(*c->touches));
    }
    l_arcCommit(c->camera, &properties);
    if (c->poseWritten)
    {
        c->camera->node.dirty = true;
    }
    *out = effects;
    return BL_OK;
}
