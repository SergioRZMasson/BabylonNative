#include "LiteInternal.h"
#include <bx/cpu.h>
#include <bx/os.h>

static L_Engine* engines;
static L_Engine* ownedEngine;
static volatile uint32_t engineLock;

static bool lockEngines()
{
    return bx::atomicCompareAndSwap(&engineLock, UINT32_C(0), UINT32_C(1)) == 0;
}

static void unlockEngines()
{
    bx::atomicCompareAndSwap(&engineLock, UINT32_C(1), UINT32_C(0));
}

static bgfx::RendererType::Enum nativeBackend(bl_RendererBackend b)
{
    switch (b)
    {
        case BL_RENDERER_D3D11:
            return bgfx::RendererType::Direct3D11;
        case BL_RENDERER_D3D12:
            return bgfx::RendererType::Direct3D12;
        case BL_RENDERER_VULKAN:
            return bgfx::RendererType::Vulkan;
        case BL_RENDERER_METAL:
            return bgfx::RendererType::Metal;
        case BL_RENDERER_OPENGL:
            return bgfx::RendererType::OpenGL;
        case BL_RENDERER_OPENGLES:
            return bgfx::RendererType::OpenGLES;
        default:
            return bgfx::RendererType::Count;
    }
}

static bl_RendererBackend liteBackend(bgfx::RendererType::Enum b)
{
    switch (b)
    {
        case bgfx::RendererType::Direct3D11:
            return BL_RENDERER_D3D11;
        case bgfx::RendererType::Direct3D12:
            return BL_RENDERER_D3D12;
        case bgfx::RendererType::Vulkan:
            return BL_RENDERER_VULKAN;
        case bgfx::RendererType::Metal:
            return BL_RENDERER_METAL;
        case bgfx::RendererType::OpenGL:
            return BL_RENDERER_OPENGL;
        case bgfx::RendererType::OpenGLES:
            return BL_RENDERER_OPENGLES;
        default:
            return BL_RENDERER_AUTO;
    }
}

static bgfx::TextureFormat::Enum colorFormat(bl_ColorFormat f)
{
    return f == BL_COLOR_BGRA8     ? bgfx::TextureFormat::BGRA8
           : f == BL_COLOR_RGBA16F ? bgfx::TextureFormat::RGBA16F
                                   : bgfx::TextureFormat::RGBA8;
}

static uint32_t resetFlags(const bl_NativeEngineOptions* o)
{
    return o->vsync ? BGFX_RESET_VSYNC : 0;
}

static uint32_t swapChainFlags(uint8_t sampleCount)
{
    uint32_t flags = 0;
    switch (sampleCount)
    {
        case 2:
            flags |= BGFX_SWAP_CHAIN_MSAA_X2;
            break;
        case 4:
            flags |= BGFX_SWAP_CHAIN_MSAA_X4;
            break;
        case 8:
            flags |= BGFX_SWAP_CHAIN_MSAA_X8;
            break;
        case 16:
            flags |= BGFX_SWAP_CHAIN_MSAA_X16;
            break;
        default:
            break;
    }
    return flags;
}

static void describeSwapChain(const bl_NativeEngineOptions* native, bgfx::SwapChain* chain)
{
    chain->nwh = native->platform.nativeWindowHandle;
    chain->ndt = native->platform.nativeDisplayType;
    chain->width = native->target.width;
    chain->height = native->target.height;
    chain->flags = swapChainFlags(native->target.sampleCount);
    chain->formatColor = colorFormat(native->target.colorFormat);
    chain->formatDepthStencil =
        native->target.depthFormat == BL_DEPTH_NONE   ? bgfx::TextureFormat::Count
        : native->target.depthFormat == BL_DEPTH_D32F ? bgfx::TextureFormat::D32F
                                                      : bgfx::TextureFormat::D24S8;
}

static bl_Status validateTarget(bl_Runtime* r, const bl_NativeTarget* t, L_Engine* self,
                                bool initialized)
{
    if (!t || !t->width || !t->height || t->width > UINT16_MAX || t->height > UINT16_MAX ||
        !t->viewCount || (unsigned)t->kind > BL_TARGET_BGFX_FRAMEBUFFER ||
        (unsigned)t->colorFormat > BL_COLOR_RGBA16F || (unsigned)t->depthFormat > BL_DEPTH_D32F ||
        (t->kind == BL_TARGET_BGFX_FRAMEBUFFER && t->framebufferIndex == BL_INVALID_BGFX_HANDLE) ||
        (t->sampleCount != 1 && t->sampleCount != 2 && t->sampleCount != 4 && t->sampleCount != 8 &&
         t->sampleCount != 16) ||
        (uint32_t)t->firstViewId + t->viewCount > 65536)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid native target");
    }
    if (t->colorFormat == BL_COLOR_RGBA16F)
    {
        return L_FAIL(r, BL_UNSUPPORTED,
                      "RGBA16F targets require a floating-point clear-palette ownership contract");
    }
    for (L_Engine* e = engines; e; e = e->globalNext)
    {
        if (e != self)
        {
            const bl_NativeTarget* other = &e->native.target;
            if (t->firstViewId < (uint32_t)other->firstViewId + other->viewCount &&
                other->firstViewId < (uint32_t)t->firstViewId + t->viewCount)
            {
                return L_FAIL(r, BL_BUSY, "Overlapping host view ranges");
            }
        }
    }
    if (initialized && (uint32_t)t->firstViewId + t->viewCount > bgfx::getCaps()->limits.maxViews)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "View range exceeds backend limits");
    }
    return BL_OK;
}

static void completion(bl_Runtime* r, L_Engine* e, bl_Status status)
{
    bl_CompletionCallback fn = e->completion;
    void* user = e->completionUser;
    e->completion = NULL;
    e->completionUser = NULL;
    if (fn)
    {
        l_enterDispatch(r);
        fn(user, status);
        l_leaveDispatch(r);
    }
}

bl_Status l_retire(L_Engine* e)
{
    bl_Runtime* r = e->runtime;
    if (l_inFrame() || l_inDispatch())
    {
        return L_FAIL(r, BL_BUSY, "Cannot wait inside a frame/callback dispatch");
    }
    if (e->native.ownership == BL_BGFX_BORROWED)
    {
        bl_Status status;
        L_CALL(r, status, e->native.waitForSubmittedWork(e->native.hostUserData));
        return status == BL_OK ? BL_OK : L_FAIL(r, status, "Host GPU wait failed");
    }
    if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK))
    {
        return L_FAIL(r, BL_UNSUPPORTED, "Backend lacks an owned GPU readback fence");
    }
    bgfx::TextureHandle fence =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK);
    if (!bgfx::isValid(fence))
    {
        return L_FAIL(r, BL_OUT_OF_MEMORY, "GPU fence allocation failed");
    }
    bgfx::TextureRegion region;
    region.init(fence);
    uint32_t pixel = 0;
    uint32_t finish = bgfx::read(region, &pixel);
    uint32_t current;
    do
    {
        current = bgfx::frame();
    }
    while ((int32_t)(current - finish) < 0);
    bgfx::destroy(fence);
    bgfx::frame();
    return BL_OK;
}

void l_unregisterScene(L_Scene* s)
{
    if (!s->registered)
    {
        return;
    }
    L_Engine* e = s->engine;
    L_Scene** p = &e->scenes;
    while (*p && *p != s)
    {
        p = &(*p)->next;
    }
    if (*p)
    {
        *p = s->next;
    }
    e->lastScene = NULL;
    for (L_Scene* cur = e->scenes; cur; cur = cur->next)
    {
        e->lastScene = cur;
    }
    s->next = NULL;
    s->registered = false;
}

void l_sceneCleanup(bl_Runtime* r, L_Record* record)
{
    L_Scene* s = (L_Scene*)record;
    l_unregisterScene(s);
    l_enterDispatch(r);
    for (L_Callback* p = s->dispose; p; p = p->next)
    {
        p->dispose(p->user);
    }
    l_leaveDispatch(r);
    for (size_t i = 0; i < s->memberCount; ++i)
    {
        if (s->memberOwners[i])
        {
            L_Node* n = s->members[i];
            if (n->sceneCount)
            {
                --n->sceneCount;
            }
            l_parent(n, NULL);
            if (!n->sceneCount && n->record.kind == L_MESH && !n->record.disposed)
            {
                n->record.cleanup(r, &n->record);
            }
        }
    }
    for (size_t i = 0; i < s->memberCount; ++i)
    {
        l_unpin(&s->members[i]->record);
    }
    s->memberCount = 0;
    s->properties.camera = {};
    L_Callback* p = s->before;
    while (p)
    {
        L_Callback* next = p->next;
        l_free(r, p);
        p = next;
    }
    p = s->dispose;
    while (p)
    {
        L_Callback* next = p->next;
        l_free(r, p);
        p = next;
    }
    s->before = s->dispose = NULL;
    l_free(r, s->members);
    l_free(r, s->memberOwners);
    s->members = NULL;
    s->memberOwners = NULL;
    l_free(r, s->drawScratch);
    l_free(r, s->groupScratch);
    s->drawScratch = NULL;
    s->groupScratch = NULL;
    s->drawCapacity = 0;
    s->groupCapacity = 0;
    l_unpin(&s->engine->record);
}

static void engineCleanup(bl_Runtime* r, L_Record* record)
{
    L_Engine* e = (L_Engine*)record;
    e->running = false;
    completion(r, e, BL_CANCELLED);
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (p && p->kind == L_SCENE && !p->disposed && ((L_Scene*)p)->engine == e)
        {
            p->cleanup(r, p);
            p->disposed = true;
        }
    }
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (p && p->kind == L_MESH && !p->disposed && ((L_Mesh*)p)->engine == e)
        {
            p->cleanup(r, p);
            p->disposed = true;
        }
    }
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (!p)
        {
            continue;
        }
        if (p->kind == L_MATERIAL && !p->disposed && ((L_Material*)p)->engine == e)
        {
            L_Material* m = (L_Material*)p;
            if (m->graphicsCleanup)
            {
                m->graphicsCleanup(r, m);
            }
        }
        if (p->kind == L_TEXTURE && !p->disposed && ((L_Texture*)p)->engine == e)
        {
            p->cleanup(r, p);
            p->disposed = true;
        }
    }
    record->cleanupStatus = l_retire(e);
    if (record->cleanupStatus != BL_OK)
    {
        return;
    }
    while (!lockEngines())
    {
        bx::yield();
    }
    L_Engine** p = &engines;
    while (*p && *p != e)
    {
        p = &(*p)->globalNext;
    }
    if (*p)
    {
        *p = e->globalNext;
    }
    if (e->native.ownership == BL_BGFX_OWNED)
    {
        bgfx::shutdown();
        ownedEngine = NULL;
    }
    unlockEngines();
}

static bl_Status enginePreDispose(bl_Runtime*, L_Record* record)
{
    return l_retire((L_Engine*)record);
}

bl_Status bl_createEngine(bl_Runtime* r, const bl_NativeEngineOptions* native,
                          const bl_EngineOptions* o, bl_EngineContext* out)
{
    L_TRY(l_check(r));
    if (!native || !out || !native->isExternalBgfxInitialized ||
        (unsigned)native->ownership > BL_BGFX_BORROWED ||
        (unsigned)native->backend > BL_RENDERER_OPENGLES ||
        (o && (unsigned)o->useHighPrecisionMatrix > BL_BOOL_TRUE) ||
        (native->ownership == BL_BGFX_BORROWED && !native->waitForSubmittedWork))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (native->platform.backBuffer || native->platform.backBufferDepthStencil)
    {
        return L_FAIL(r, BL_UNSUPPORTED,
                      "The pinned bgfx API cannot import native back-buffer pointers");
    }
    if (!lockEngines())
    {
        return BL_BUSY;
    }
    if (engines && engines->runtime->thread != r->thread)
    {
        unlockEngines();
        return BL_WRONG_THREAD;
    }
    bool owned = native->ownership == BL_BGFX_OWNED;
    l_enterHost(r);
    bool external = native->isExternalBgfxInitialized(native->hostUserData);
    l_leaveHost(r);
    if (ownedEngine || (owned && (engines || external)))
    {
        unlockEngines();
        return L_FAIL(r, BL_BUSY, "bgfx process ownership conflict");
    }
    if (!owned && !external)
    {
        unlockEngines();
        return L_FAIL(r, BL_NOT_READY, "Host bgfx is not initialized");
    }
    bl_Status status = validateTarget(r, &native->target, NULL, !owned);
    if (status != BL_OK)
    {
        unlockEngines();
        return status;
    }
    L_Record* record;
    status = l_record(r, sizeof(L_Engine), L_ENGINE, 0, engineCleanup, &record);
    if (status != BL_OK)
    {
        unlockEngines();
        return status;
    }
    L_Engine* e = (L_Engine*)record;
    e->runtime = r;
    e->native = *native;
    e->highPrecision = o && o->useHighPrecisionMatrix == BL_BOOL_TRUE;
    e->record.preDispose = enginePreDispose;
    if (owned)
    {
        if (native->singleThreadedRenderer)
        {
            bgfx::renderFrame();
        }
        bgfx::Init init;
        init.type = nativeBackend(native->backend);
        init.platformData.context = native->platform.context;
        describeSwapChain(native, &init.swapChain);
        init.reset = resetFlags(native);
        if (!bgfx::init(init))
        {
            e->record.disposed = true;
            unlockEngines();
            return L_FAIL(r, BL_DEVICE_LOST, "bgfx initialization failed");
        }
    }
    e->backend = liteBackend(bgfx::getRendererType());
    if (e->backend == BL_RENDERER_AUTO ||
        (native->backend != BL_RENDERER_AUTO && native->backend != e->backend) ||
        (uint32_t)native->target.firstViewId + native->target.viewCount >
            bgfx::getCaps()->limits.maxViews)
    {
        if (owned)
        {
            bgfx::shutdown();
        }
        e->record.disposed = true;
        unlockEngines();
        return L_FAIL(r, BL_UNSUPPORTED, "Concrete renderer backend/target unavailable");
    }
    if (owned)
    {
        ownedEngine = e;
    }
    e->globalNext = engines;
    engines = e;
    unlockEngines();
    *out = {r, e->record.id};
    return BL_OK;
}

bl_Status bl_disposeEngine(bl_EngineContext h)
{
    L_RETIRED(h, L_ENGINE, L_Engine, e);
    if (!e || e->record.disposed)
    {
        return BL_OK;
    }
    if (l_inFrame() || l_inDispatch())
    {
        return BL_BUSY;
    }
    L_TRY(l_retire(e));
    engineCleanup(h._runtime, &e->record);
    if (e->record.cleanupStatus != BL_OK)
    {
        return e->record.cleanupStatus;
    }
    e->record.disposed = true;
    return BL_OK;
}

bl_Status bl_getEngineStats(bl_EngineContext h, bl_EngineStats* out)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = e->stats;
    return BL_OK;
}

bl_Status bl_setNativeTarget(bl_EngineContext h, const bl_NativeTarget* target)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    if (l_inFrame() || l_inDispatch())
    {
        return BL_BUSY;
    }
    if (!lockEngines())
    {
        return BL_BUSY;
    }
    bl_Status s = validateTarget(h._runtime, target, e, true);
    size_t count = 0;
    for (L_Scene* p = e->scenes; p; p = p->next)
    {
        ++count;
    }
    if (s == BL_OK && count > target->viewCount)
    {
        s = BL_INVALID_ARGUMENT;
    }
    if (s == BL_OK)
    {
        bool reset = e->native.target.width != target->width ||
                     e->native.target.height != target->height ||
                     e->native.target.sampleCount != target->sampleCount ||
                     e->native.target.colorFormat != target->colorFormat ||
                     e->native.target.depthFormat != target->depthFormat;
        e->native.target = *target;
        if (reset && e->native.ownership == BL_BGFX_OWNED)
        {
            bgfx::SwapChain chain;
            describeSwapChain(&e->native, &chain);
            bgfx::reset(resetFlags(&e->native),
                        e->native.platform.nativeWindowHandle ? &chain : NULL);
        }
    }
    unlockEngines();
    return s;
}

bl_Status bl_waitForGpuIdle(bl_EngineContext h)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    return l_retire(e);
}

bl_Status bl_waitForGpuResourceRetirements(bl_EngineContext h)
{
    return bl_waitForGpuIdle(h);
}

bl_Status bl_startEngine(bl_EngineContext h, bl_CompletionCallback fn, void* user)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    if (e->inFrame || e->running)
    {
        return BL_BUSY;
    }
    e->running = true;
    e->first = true;
    e->completion = fn;
    e->completionUser = user;
    return BL_OK;
}

bl_Status bl_stopEngine(bl_EngineContext h)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    e->running = false;
    e->first = false;
    completion(h._runtime, e, BL_CANCELLED);
    return BL_OK;
}

bl_Status bl_createSceneContext(bl_EngineContext h, bl_SceneContext* out)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_NEW(h._runtime, L_SCENE, 40, l_sceneCleanup, L_Scene, s);
    s->engine = e;
    l_pin(&e->record);
    s->properties.clearColor = {.2, .2, .3, 1};
    *out = {h._runtime, s->record.id};
    return BL_OK;
}

bl_Status bl_getSceneProperties(bl_SceneContext h, bl_SceneProperties* out)
{
    L_GET(h, L_SCENE, L_Scene, s);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = s->properties;
    return BL_OK;
}

bl_Status bl_setSceneProperties(bl_SceneContext h, const bl_SceneProperties* p)
{
    L_GET(h, L_SCENE, L_Scene, s);
    if (!p || !isfinite(p->fixedDeltaMs) || !isfinite(p->clearColor.r) ||
        !isfinite(p->clearColor.g) || !isfinite(p->clearColor.b) || !isfinite(p->clearColor.a))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!L_NULL(p->camera))
    {
        if (p->camera._runtime != h._runtime)
        {
            return BL_WRONG_RUNTIME;
        }
        L_GET(p->camera, L_CAMERA, L_Node, camera);
        camera->highPrecision = s->engine->highPrecision;
        camera->dirty = true;
    }
    s->properties = *p;
    return BL_OK;
}

static bl_Status inspectHierarchy(bl_Runtime* r, L_Scene* s, L_Node* n, size_t* count, size_t depth)
{
    if (depth > r->count || n->record.disposed)
    {
        return n->record.disposed ? BL_DISPOSED : BL_INVALID_ARGUMENT;
    }
    if (*count == SIZE_MAX)
    {
        return BL_OUT_OF_MEMORY;
    }
    ++*count;
    if (n->record.kind == L_MESH)
    {
        L_Mesh* m = (L_Mesh*)n;
        if (m->engine != s->engine)
        {
            return BL_WRONG_ENGINE;
        }
        if (s->registered && !L_NULL(m->properties.material))
        {
            L_Material* mat = (L_Material*)l_peek(r, m->properties.material._id);
            L_TRY(l_prepareMaterial(r, s->engine, mat));
            if (mat->activeAttributes & ~m->geometry.attributeMask)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    for (size_t i = 0; i < n->childCount; ++i)
    {
        L_Node* c;
        L_TRY(l_node(n->children[i], &c));
        for (L_Node* p = n; p; p = p->parent)
        {
            if (p == c)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        L_TRY(inspectHierarchy(r, s, c, count, depth + 1));
    }
    return BL_OK;
}

static void addHierarchy(bl_Runtime* r, L_Scene* s, L_Node* n)
{
    bool owner = true;
    for (size_t i = 0; i < s->memberCount; ++i)
    {
        if (s->members[i] == n && s->memberOwners[i])
        {
            owner = false;
            break;
        }
    }
    s->members[s->memberCount] = n;
    s->memberOwners[s->memberCount++] = owner;
    if (owner)
    {
        ++n->sceneCount;
    }
    l_pin(&n->record);
    n->highPrecision = s->engine->highPrecision;
    n->dirty = true;
    for (size_t i = 0; i < n->childCount; ++i)
    {
        L_Node* c = (L_Node*)l_peek(r, n->children[i]._id);
        l_parent(c, n);
        addHierarchy(r, s, c);
    }
}

bl_Status bl_addToScene(bl_SceneContext h, bl_SceneNode entity)
{
    L_GET(h, L_SCENE, L_Scene, s);
    bl_Runtime* r = h._runtime;
    if (entity._runtime != r)
    {
        return BL_WRONG_RUNTIME;
    }
    L_NODE_GET(entity, n);
    size_t count = 0;
    L_TRY(inspectHierarchy(r, s, n, &count, 0));
    if (count > SIZE_MAX - s->memberCount)
    {
        return BL_OUT_OF_MEMORY;
    }
    size_t need = s->memberCount + count;
    if (need > s->memberCapacity)
    {
        size_t cap = s->memberCapacity ? s->memberCapacity : 16;
        while (cap < need)
        {
            if (cap > SIZE_MAX / 2)
            {
                cap = need;
                break;
            }
            cap *= 2;
        }
        size_t bytes;
        if (!l_size(cap, sizeof(L_Node*), &bytes))
        {
            return BL_OUT_OF_MEMORY;
        }
        L_Node** members = (L_Node**)l_alloc(r, bytes);
        bool* owners = (bool*)l_alloc(r, cap * sizeof(bool));
        if (!members || !owners)
        {
            l_free(r, members);
            l_free(r, owners);
            return BL_OUT_OF_MEMORY;
        }
        if (s->memberCount)
        {
            memcpy(members, s->members, s->memberCount * sizeof(*members));
            memcpy(owners, s->memberOwners, s->memberCount * sizeof(*owners));
        }
        l_free(r, s->members);
        l_free(r, s->memberOwners);
        s->members = members;
        s->memberOwners = owners;
        s->memberCapacity = cap;
    }
    addHierarchy(r, s, n);
    return BL_OK;
}

bl_Status l_removeSceneNode(bl_Runtime* r, L_Scene* s, L_Node* n)
{
    if (!n || n->record.disposed)
    {
        return BL_OK;
    }
    for (size_t i = 0; i < n->childCount; ++i)
    {
        L_Node* c = (L_Node*)l_peek(r, n->children[i]._id);
        L_TRY(l_removeSceneNode(r, s, c));
    }
    l_parent(n, NULL);
    if (s->properties.camera._id == n->record.id)
    {
        s->properties.camera = {};
    }
    bool removedOwner = false;
    for (size_t i = 0; i < s->memberCount;)
    {
        if (s->members[i] != n)
        {
            ++i;
            continue;
        }
        removedOwner = removedOwner || s->memberOwners[i];
        l_unpin(&n->record);
        memmove(s->members + i, s->members + i + 1, (s->memberCount - i - 1) * sizeof(*s->members));
        memmove(s->memberOwners + i, s->memberOwners + i + 1,
                (s->memberCount - i - 1) * sizeof(*s->memberOwners));
        --s->memberCount;
    }
    if (removedOwner && n->sceneCount)
    {
        --n->sceneCount;
    }
    if (removedOwner && !n->sceneCount && n->record.kind == L_MESH)
    {
        n->record.cleanup(r, &n->record);
    }
    return BL_OK;
}

bl_Status bl_removeFromScene(bl_SceneContext h, bl_SceneNode entity)
{
    L_GET(h, L_SCENE, L_Scene, s);
    if (entity._runtime != h._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_Node* n;
    L_TRY(l_node(entity, &n, true));
    return l_removeSceneNode(h._runtime, s, n);
}

bl_Status bl_registerScene(bl_SceneContext h)
{
    L_GET(h, L_SCENE, L_Scene, s);
    if (s->registered)
    {
        return BL_OK;
    }
    if (l_inDispatch())
    {
        return BL_BUSY;
    }
    size_t count = 0;
    for (L_Scene* p = s->engine->scenes; p; p = p->next)
    {
        ++count;
    }
    if (count >= s->engine->native.target.viewCount)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < s->memberCount; ++i)
    {
        L_Node* n = s->members[i];
        if (n->record.disposed || n->record.kind != L_MESH)
        {
            continue;
        }
        L_Mesh* m = (L_Mesh*)n;
        if (L_NULL(m->properties.material))
        {
            continue;
        }
        L_Material* mat = (L_Material*)l_peek(h._runtime, m->properties.material._id);
        L_TRY(l_prepareMaterial(h._runtime, s->engine, mat));
        if (mat->activeAttributes & ~m->geometry.attributeMask)
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    if (s->engine->lastScene)
    {
        s->engine->lastScene->next = s;
    }
    else
    {
        s->engine->scenes = s;
    }
    s->engine->lastScene = s;
    s->registered = true;
    return BL_OK;
}

bl_Status bl_unregisterScene(bl_SceneContext h)
{
    L_GET(h, L_SCENE, L_Scene, s);
    if (l_inDispatch())
    {
        return BL_BUSY;
    }
    l_unregisterScene(s);
    return BL_OK;
}

bl_Status bl_disposeScene(bl_SceneContext h)
{
    L_RETIRED(h, L_SCENE, L_Scene, s);
    if (!s || s->record.disposed)
    {
        return BL_OK;
    }
    if (l_inDispatch())
    {
        return BL_BUSY;
    }
    l_sceneCleanup(h._runtime, &s->record);
    s->record.disposed = true;
    return BL_OK;
}

static bl_Status callback(bl_SceneContext h, bl_BeforeRenderCallback before,
                          bl_SceneDisposeCallback dispose, void* user, bl_CallbackToken* out)
{
    L_GET(h, L_SCENE, L_Scene, s);
    bl_Runtime* r = h._runtime;
    if (!out || (!before && !dispose))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (l_inDispatch())
    {
        return BL_BUSY;
    }
    L_Callback* p = (L_Callback*)l_alloc(r, sizeof(*p));
    if (!p)
    {
        return BL_OUT_OF_MEMORY;
    }
    p->id = ++r->callbackId;
    p->before = before;
    p->dispose = dispose;
    p->user = user;
    if (before)
    {
        p->next = s->before;
        s->before = p;
    }
    else
    {
        L_Callback** tail = &s->dispose;
        while (*tail)
        {
            tail = &(*tail)->next;
        }
        *tail = p;
    }
    *out = {p->id};
    return BL_OK;
}

bl_Status bl_onBeforeRender(bl_SceneContext h, bl_BeforeRenderCallback fn, void* user,
                            bl_CallbackToken* out)
{
    return callback(h, fn, NULL, user, out);
}

bl_Status bl_onSceneDispose(bl_SceneContext h, bl_SceneDisposeCallback fn, void* user,
                            bl_CallbackToken* out)
{
    return callback(h, NULL, fn, user, out);
}

bl_Status bl_removeSceneCallback(bl_SceneContext h, bl_CallbackToken token)
{
    L_GET(h, L_SCENE, L_Scene, s);
    if (l_inDispatch())
    {
        return BL_BUSY;
    }
    L_Callback** heads[2] = {&s->before, &s->dispose};
    for (unsigned i = 0; i < 2; ++i)
    {
        L_Callback** p = heads[i];
        while (*p)
        {
            if ((*p)->id == token.value)
            {
                L_Callback* old = *p;
                *p = old->next;
                l_free(h._runtime, old);
                return BL_OK;
            }
            p = &(*p)->next;
        }
    }
    return BL_INVALID_ARGUMENT;
}

static uint8_t colorByte(double d)
{
    if (d < 0)
    {
        d = 0;
    }
    if (d > 1)
    {
        d = 1;
    }
    return (uint8_t)(d * 255 + .5);
}

static bl_Status renderScene(bl_Runtime* r, L_Engine* e, L_Scene* s, double delta, uint16_t viewId,
                             bool first)
{
    l_enterDispatch(r);
    for (L_Callback* p = s->before; p; p = p->next)
    {
        p->before(p->user, s->properties.fixedDeltaMs > 0 ? s->properties.fixedDeltaMs : delta);
    }
    l_leaveDispatch(r);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::FrameBufferHandle target = {(uint16_t)(e->native.target.kind == BL_TARGET_BGFX_FRAMEBUFFER
                                                     ? e->native.target.framebufferIndex
                                                     : BL_INVALID_BGFX_HANDLE)};
    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, (uint16_t)e->native.target.width,
                      (uint16_t)e->native.target.height);
    bl_Color4 c = s->properties.clearColor;
    uint32_t rgba = ((uint32_t)colorByte(c.r) << 24) | ((uint32_t)colorByte(c.g) << 16) |
                    ((uint32_t)colorByte(c.b) << 8) | colorByte(c.a);
    uint16_t flags = e->native.target.depthFormat == BL_DEPTH_NONE ? 0 : BGFX_CLEAR_DEPTH;
    if (first)
    {
        flags |= BGFX_CLEAR_COLOR;
    }
    bgfx::setViewClear(viewId, flags, rgba, 0, 0);
    bgfx::touch(viewId);
    if (L_NULL(s->properties.camera))
    {
        return BL_OK;
    }
    L_Record* cameraRecord;
    L_TRY(l_get(r, s->properties.camera._id, L_CAMERA, &cameraRecord));
    L_Node* camera = (L_Node*)cameraRecord;
    L_TRY(l_world(r, camera));
    bl_Mat4 view;
    bl_Mat4 projection = {};
    l_inverse(&camera->world, &view, e->highPrecision);
    double tanValue = 1 / tan(camera->camera.fov * .5);
    double range = camera->camera.farPlane - camera->camera.nearPlane;
    projection.values[0] = tanValue / ((double)e->native.target.width / e->native.target.height);
    projection.values[5] = tanValue;
    projection.values[10] = -camera->camera.nearPlane / range;
    projection.values[11] = 1;
    projection.values[14] = camera->camera.farPlane * camera->camera.nearPlane / range;
    if (!e->highPrecision)
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            projection.values[i] = (float)projection.values[i];
        }
    }
    size_t count = 0;
    L_TRY(l_collectDraws(r, s, &view, &count));
    L_Draw* draws = (L_Draw*)s->drawScratch;
    bl_Status status = BL_OK;
    if (count)
    {
        l_sortDraws(s, count);
        bl_Vec3 position = {camera->world.values[12], camera->world.values[13],
                            camera->world.values[14]};
        for (size_t i = 0; i < count; ++i)
        {
            status = l_drawMaterial(r, e, draws[i].material, draws[i].mesh, &view, &projection,
                                    position, viewId);
            if (status != BL_OK)
            {
                break;
            }
        }
    }
    return status;
}

static bl_Status render(L_Engine* e, double delta, bool driven)
{
    bl_Runtime* r = e->runtime;
    if (!isfinite(delta) || delta < 0)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (l_inFrame() || l_inDispatch())
    {
        return BL_BUSY;
    }
    if (driven && !e->running)
    {
        return BL_STOPPED;
    }
    bool first = driven && e->first;
    if (first)
    {
        delta = 0;
    }
    e->inFrame = true;
    l_enterFrame(r);
    e->stats.drawCallCount = 0;
    bl_Status status = BL_OK;
    unsigned view = 0;
    for (L_Scene* s = e->scenes; s; s = s->next, ++view)
    {
        status =
            renderScene(r, e, s, delta, (uint16_t)(e->native.target.firstViewId + view), view == 0);
        if (status != BL_OK)
        {
            break;
        }
    }
    if (e->native.ownership == BL_BGFX_OWNED)
    {
        bgfx::frame();
    }
    e->inFrame = false;
    l_leaveFrame(r);
    const bgfx::Stats* stats = bgfx::getStats();
    e->stats.gpuFrameTimeMs =
        stats && stats->gpuTimerFreq > 0 && stats->gpuTimeEnd >= stats->gpuTimeBegin
            ? (double)(stats->gpuTimeEnd - stats->gpuTimeBegin) * 1000.0 /
                  (double)stats->gpuTimerFreq
            : 0;
    if (status == BL_OK && first && e->running)
    {
        e->first = false;
        completion(r, e, BL_OK);
    }
    return status;
}

bl_Status bl_frame(bl_EngineContext h, double delta)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    return render(e, delta, true);
}

bl_Status bl_renderFrame(bl_EngineContext h, double delta)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    return render(e, delta, false);
}
