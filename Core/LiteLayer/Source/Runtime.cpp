#include "RuntimeInternal.h"
#include <bx/os.h>
#include <bx/cpu.h>

struct L_Allocation
{
    size_t size;
    uint64_t padding;
};

static thread_local unsigned hostCalls;
static thread_local unsigned frameCalls;
static thread_local unsigned dispatchCalls;
static volatile uint64_t identitySerial;

void l_enterHost(bl_Runtime* r)
{
    ++hostCalls;
    if (r)
    {
        ++r->hostDepth;
    }
}

void l_leaveHost(bl_Runtime* r)
{
    if (r)
    {
        --r->hostDepth;
    }
    --hostCalls;
}

bool l_inHost()
{
    return hostCalls != 0;
}

void l_enterFrame(bl_Runtime* r)
{
    ++frameCalls;
    ++r->frameDepth;
}

void l_leaveFrame(bl_Runtime* r)
{
    --r->frameDepth;
    --frameCalls;
}

bool l_inFrame()
{
    return frameCalls != 0;
}

void l_enterDispatch(bl_Runtime* r)
{
    ++dispatchCalls;
    ++r->dispatchDepth;
}

void l_leaveDispatch(bl_Runtime* r)
{
    --r->dispatchDepth;
    --dispatchCalls;
}

bool l_inDispatch()
{
    return dispatchCalls != 0;
}

void l_pin(L_Record* p)
{
    if (p)
    {
        ++p->pins;
    }
}

void l_unpin(L_Record* p)
{
    if (p && p->pins)
    {
        --p->pins;
    }
}

L_Record* l_peek(bl_Runtime* r, uint64_t id)
{
    uint32_t index = (uint32_t)id;
    if (!index || index > r->count || r->slots[index - 1].generation != (uint32_t)(id >> 32))
    {
        return NULL;
    }
    return r->records[index - 1];
}

static void collectRecords(bl_Runtime* r)
{
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (!p || !p->disposed || p->pins)
        {
            continue;
        }
        L_Slot* slot = r->slots + i;
        if (slot->generation != UINT32_MAX)
        {
            slot->nextFree = r->freeHeads[slot->kind];
            r->freeHeads[slot->kind] = i + 1;
        }
        l_free(r, p);
        r->records[i] = NULL;
    }
}

bool l_size(size_t a, size_t b, size_t* out)
{
    if (b && a > SIZE_MAX / b)
    {
        return false;
    }
    *out = a * b;
    return true;
}

bool l_span(const void* p, size_t n)
{
    return p || !n;
}

bool l_typedSpan(const void* p, size_t n, size_t alignment)
{
    return l_span(p, n) && (!p || !((uintptr_t)p & (alignment - 1)));
}

bool l_string(bl_String s)
{
    if (!l_span(s.data, s.length))
    {
        return false;
    }
    const uint8_t* bytes = (const uint8_t*)s.data;
    size_t offset = 0;
    while (offset < s.length)
    {
        uint8_t first = bytes[offset++];
        if (!first)
        {
            return false;
        }
        if (first < 0x80)
        {
            continue;
        }
        unsigned extra = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            extra = 1;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            extra = 2;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            extra = 3;
        }
        else
        {
            return false;
        }
        if (extra > s.length - offset)
        {
            return false;
        }
        uint8_t second = bytes[offset];
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
        {
            return false;
        }
        for (unsigned i = 0; i < extra; ++i)
        {
            uint8_t next = bytes[offset++];
            if (next < 0x80 || next > 0xbf)
            {
                return false;
            }
        }
    }
    return true;
}

bool l_equal(bl_String a, bl_String b)
{
    return a.length == b.length && (!a.length || !memcmp(a.data, b.data, a.length));
}

bool l_name(bl_String a, const char* b)
{
    bl_String s = {b, strlen(b)};
    return l_equal(a, s);
}

bool l_identifier(bl_String s)
{
    if (!l_string(s) || !s.length)
    {
        return false;
    }
    for (size_t i = 0; i < s.length; ++i)
    {
        char c = s.data[i];
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (i && c >= '0' && c <= '9')))
        {
            return false;
        }
    }
    return true;
}

bl_Status l_error(bl_Runtime* r, bl_Status status, const char* operation, const char* message,
                  uint32_t code)
{
    if (!r)
    {
        return status;
    }
    size_t n = strlen(message);
    if (n >= sizeof(r->errorMessage))
    {
        n = sizeof(r->errorMessage) - 1;
        while (n && ((uint8_t)message[n] & 0xc0) == 0x80)
        {
            --n;
        }
    }
    memcpy(r->errorMessage, message, n);
    r->errorMessage[n] = 0;
    r->error = {status, code, {operation, strlen(operation)}, {r->errorMessage, n}};
    if (r->errorCallback && !r->hostDepth)
    {
        l_enterHost(r);
        r->errorCallback(r->errorUser, &r->error);
        l_leaveHost(r);
    }
    return status;
}

bl_Status l_check(bl_Runtime* r)
{
    if (!r)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (r->thread != bx::getTid())
    {
        return BL_WRONG_THREAD;
    }
    if (l_inHost() || r->destroying)
    {
        return BL_BUSY;
    }
    return BL_OK;
}

bool l_allocationFits(size_t bytes)
{
    return bytes <= SIZE_MAX - sizeof(L_Allocation);
}

void* l_alloc(bl_Runtime* r, size_t bytes)
{
    if (!bytes)
    {
        return NULL;
    }
    if (!l_allocationFits(bytes))
    {
        l_error(r, BL_OUT_OF_MEMORY, "allocate", "Allocation size overflow");
        return NULL;
    }
    size_t total = bytes + sizeof(L_Allocation);
    l_enterHost(r);
    L_Allocation* a = (L_Allocation*)r->allocator.allocate(r->allocator.userData, total, 16);
    l_leaveHost(r);
    if (!a)
    {
        l_error(r, BL_OUT_OF_MEMORY, "allocate", "Allocator returned NULL");
        return NULL;
    }
    if ((uintptr_t)a & 15)
    {
        l_enterHost(r);
        r->allocator.deallocate(r->allocator.userData, a, total, 16);
        l_leaveHost(r);
        l_error(r, BL_OUT_OF_MEMORY, "allocate", "Allocator returned misaligned storage");
        return NULL;
    }
    a->size = total;
    a->padding = 0;
    memset(a + 1, 0, bytes);
    return a + 1;
}

void l_free(bl_Runtime* r, void* p)
{
    if (!p)
    {
        return;
    }
    L_Allocation* a = (L_Allocation*)p - 1;
    l_enterHost(r);
    r->allocator.deallocate(r->allocator.userData, a, a->size, 16);
    l_leaveHost(r);
}

bl_String l_copyString(bl_Runtime* r, bl_String s)
{
    bl_String copy = {};
    if (s.length == SIZE_MAX)
    {
        return copy;
    }
    char* p = (char*)l_alloc(r, s.length + 1);
    if (!p)
    {
        return copy;
    }
    if (s.length)
    {
        memcpy(p, s.data, s.length);
    }
    copy.data = p;
    copy.length = s.length;
    return copy;
}

bl_Status l_record(bl_Runtime* r, size_t bytes, unsigned kind, int priority,
                   void (*cleanup)(bl_Runtime*, L_Record*), L_Record** out)
{
    L_TRY(l_check(r));
    collectRecords(r);
    size_t reuse = r->freeHeads[kind];
    if (!reuse && r->count >= UINT32_MAX - 1)
    {
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Identity space exhausted");
    }
    if (!reuse && r->count == r->capacity)
    {
        size_t n = r->capacity ? r->capacity * 2 : 32;
        size_t sz;
        if (!l_size(n, sizeof(L_Record*), &sz))
        {
            return L_FAIL(r, BL_OUT_OF_MEMORY, "Registry overflow");
        }
        L_Record** p = (L_Record**)l_alloc(r, sz);
        if (!p)
        {
            return L_FAIL(r, BL_OUT_OF_MEMORY, "Registry allocation failed");
        }
        size_t slotBytes;
        if (!l_size(n, sizeof(L_Slot), &slotBytes))
        {
            l_free(r, p);
            return L_FAIL(r, BL_OUT_OF_MEMORY, "Slot size overflow");
        }
        L_Slot* slots = (L_Slot*)l_alloc(r, slotBytes);
        if (!slots)
        {
            l_free(r, p);
            return L_FAIL(r, BL_OUT_OF_MEMORY, "Slot allocation failed");
        }
        if (r->count)
        {
            memcpy(p, r->records, r->count * sizeof(*p));
        }
        if (r->count)
        {
            memcpy(slots, r->slots, r->count * sizeof(*slots));
        }
        l_free(r, r->records);
        l_free(r, r->slots);
        r->records = p;
        r->slots = slots;
        r->capacity = n;
    }
    L_Record* p = (L_Record*)l_alloc(r, bytes);
    if (!p)
    {
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Object allocation failed");
    }
    uint64_t serial;
    for (;;)
    {
        uint64_t old = bx::atomicFetchAndAdd(&identitySerial, UINT64_C(0));
        if (old == UINT64_MAX)
        {
            l_free(r, p);
            return L_FAIL(r, BL_OUT_OF_MEMORY, "Allocation serial space exhausted");
        }
        if (bx::atomicCompareAndSwap(&identitySerial, old, old + 1) == old)
        {
            serial = old + 1;
            break;
        }
    }
    size_t index = reuse ? reuse - 1 : r->count++;
    uint32_t generation = reuse ? r->slots[index].generation + 1 : 1;
    if (reuse)
    {
        r->freeHeads[kind] = r->slots[index].nextFree;
    }
    r->slots[index] = {generation, kind, 0};
    p->id = ((uint64_t)generation << 32) | (index + 1);
    p->serial = serial;
    p->kind = kind;
    p->cleanup = cleanup;
    p->cleanupPriority = priority;
    r->records[index] = p;
    *out = p;
    return BL_OK;
}

bl_Status l_get(bl_Runtime* r, uint64_t id, unsigned kind, L_Record** out, bool disposedOK)
{
    if (!r)
    {
        return BL_INVALID_HANDLE;
    }
    L_TRY(l_check(r));
    uint32_t index = (uint32_t)id;
    if (!index || index > r->count || !(id >> 32))
    {
        return L_FAIL(r, BL_INVALID_HANDLE, "Unissued identity");
    }
    L_Slot* slot = r->slots + index - 1;
    if ((id >> 32) > slot->generation || (kind && slot->kind != kind))
    {
        return L_FAIL(r, BL_INVALID_HANDLE, "Wrong identity type/generation");
    }
    L_Record* p = r->records[index - 1];
    if ((id >> 32) != slot->generation || !p || p->disposed)
    {
        if (!disposedOK)
        {
            return L_FAIL(r, BL_DISPOSED, "Disposed identity");
        }
        *out = p && p->id == id ? p : NULL;
        return BL_OK;
    }
    *out = p;
    return BL_OK;
}

static bl_Status runtimeCreationError(const bl_RuntimeOptions* options, bl_Status status,
                                      const char* message)
{
    if (options->onError)
    {
        bl_Error error = {status, 0, {"bl_createRuntime", 16}, {message, strlen(message)}};
        l_enterHost(NULL);
        options->onError(options->errorUserData, &error);
        l_leaveHost(NULL);
    }
    return status;
}

bl_Status bl_createRuntime(const bl_RuntimeOptions* o, bl_Runtime** out)
{
    if (l_inHost())
    {
        return BL_BUSY;
    }
    if (!o || !out || !o->allocator.allocate || !o->allocator.deallocate ||
        (o->shaderCompiler && (!o->shaderCompiler->compile || !o->shaderCompiler->release)))
    {
        return BL_INVALID_ARGUMENT;
    }
    l_enterHost(NULL);
    bl_Runtime* r =
        (bl_Runtime*)o->allocator.allocate(o->allocator.userData, sizeof(bl_Runtime), 16);
    l_leaveHost(NULL);
    if (!r)
    {
        return runtimeCreationError(o, BL_OUT_OF_MEMORY, "Runtime allocation failed");
    }
    if ((uintptr_t)r & 15)
    {
        l_enterHost(NULL);
        o->allocator.deallocate(o->allocator.userData, r, sizeof(*r), 16);
        l_leaveHost(NULL);
        return runtimeCreationError(o, BL_INVALID_ARGUMENT,
                                    "Runtime allocator returned misaligned storage");
    }
    memset(r, 0, sizeof(*r));
    r->allocator = o->allocator;
    r->thread = bx::getTid();
    r->errorCallback = o->onError;
    r->errorUser = o->errorUserData;
    if (o->shaderCompiler)
    {
        r->compiler = *o->shaderCompiler;
        r->hasCompiler = true;
    }
    if (o->audio)
    {
        r->audio = *o->audio;
        r->hasAudio = true;
    }
    if (o->io)
    {
        r->io = *o->io;
        r->hasIO = true;
    }
    *out = r;
    return BL_OK;
}

bl_Status bl_disposeRuntime(bl_Runtime* r)
{
    L_TRY(l_check(r));
    if (l_inFrame() || l_inDispatch() || r->destroying)
    {
        return L_FAIL(r, BL_BUSY, "Runtime is busy");
    }
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (p && !p->disposed && p->preDispose)
        {
            L_TRY(p->preDispose(r, p));
        }
    }
    r->destroying = true;
    for (;;)
    {
        L_Record* next = NULL;
        for (size_t i = 0; i < r->count; ++i)
        {
            L_Record* p = r->records[i];
            if (p && !p->disposed && p->cleanup &&
                (!next || p->cleanupPriority > next->cleanupPriority))
            {
                next = p;
            }
        }
        if (!next)
        {
            break;
        }
        next->cleanupStatus = BL_OK;
        next->cleanup(r, next);
        if (next->cleanupStatus != BL_OK)
        {
            r->destroying = false;
            return next->cleanupStatus;
        }
        next->disposed = true;
    }
    for (size_t i = 0; i < r->count; ++i)
    {
        l_free(r, r->records[i]);
    }
    l_free(r, r->records);
    l_free(r, r->slots);
    bl_Allocator allocator = r->allocator;
    l_enterHost(NULL);
    allocator.deallocate(allocator.userData, r, sizeof(*r), 16);
    l_leaveHost(NULL);
    return BL_OK;
}

bl_Status bl_getLastError(bl_Runtime* r, bl_Error* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Missing output");
    }
    *out = r->error;
    return BL_OK;
}

bl_Status bl_getIOService(bl_Runtime* r, const bl_IOService** out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Missing output");
    }
    if (!r->hasIO)
    {
        return L_FAIL(r, BL_UNSUPPORTED, "No IO service");
    }
    *out = &r->io;
    return BL_OK;
}
