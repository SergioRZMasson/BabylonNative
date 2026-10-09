#ifndef BL_RUNTIME_INTERNAL_H
#define BL_RUNTIME_INTERNAL_H

#include "babylon_lite.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum L_Kind
{
    L_ENGINE = 1,
    L_SCENE,
    L_NODE,
    L_CAMERA,
    L_MESH,
    L_MATERIAL,
    L_TEXTURE,
    L_AUDIO,
    L_SOURCE,
    L_UI_CONTEXT,
    L_UI_ELEMENT,
    L_DATA,
    L_ARC_CAMERA,
    L_LIGHT,
    L_CONTROL,
    L_LIMIT,
    L_STANDARD
};

struct L_Record
{
    uint64_t id;
    uint64_t serial;
    unsigned kind;
    bool disposed;
    int cleanupPriority;
    void (*cleanup)(bl_Runtime*, L_Record*);
    bl_Status (*preDispose)(bl_Runtime*, L_Record*);
    bl_Status cleanupStatus;
    size_t pins;
};

struct L_Slot
{
    uint32_t generation;
    uint32_t kind;
    size_t nextFree;
};

struct bl_Runtime
{
    bl_Allocator allocator;
    bl_ErrorCallback errorCallback;
    void* errorUser;
    bl_ShaderCompilerService compiler;
    bl_AudioService audio;
    bl_IOService io;
    bool hasCompiler;
    bool hasAudio;
    bool hasIO;
    bool destroying;
    uint32_t thread;
    unsigned hostDepth;
    unsigned frameDepth;
    unsigned dispatchDepth;
    L_Record** records;
    L_Slot* slots;
    size_t freeHeads[L_STANDARD + 1];
    size_t count;
    size_t capacity;
    uint64_t matrixVersion;
    uint64_t callbackId;
    bl_Error error;
    char errorMessage[1024];
};

static_assert(__is_trivial(L_Record) && __is_standard_layout(L_Record),
              "Record storage must be POD");
static_assert(__is_trivial(bl_Runtime) && __is_standard_layout(bl_Runtime),
              "Runtime storage must be POD");

bl_Status l_check(bl_Runtime* r);
void l_enterHost(bl_Runtime* r);
void l_leaveHost(bl_Runtime* r);
bool l_inHost();
void l_enterFrame(bl_Runtime* r);
void l_leaveFrame(bl_Runtime* r);
bool l_inFrame();
void l_enterDispatch(bl_Runtime* r);
void l_leaveDispatch(bl_Runtime* r);
bool l_inDispatch();
bl_Status l_error(bl_Runtime* r, bl_Status status, const char* operation, const char* message,
                  uint32_t code = 0);
void* l_alloc(bl_Runtime* r, size_t bytes);
void l_free(bl_Runtime* r, void* p);
bool l_size(size_t a, size_t b, size_t* out);
bool l_allocationFits(size_t bytes);
bl_Status l_groundCounts(bl_Runtime* r, const bl_GroundOptions* options, bool gpu, size_t* vertices,
                         size_t* indices);
bool l_span(const void* p, size_t n);
bool l_typedSpan(const void* p, size_t n, size_t alignment);
bool l_string(bl_String s);
bool l_equal(bl_String a, bl_String b);
bool l_name(bl_String a, const char* b);
bool l_identifier(bl_String s);
bl_String l_copyString(bl_Runtime* r, bl_String s);
bl_Status l_record(bl_Runtime* r, size_t bytes, unsigned kind, int priority,
                   void (*cleanup)(bl_Runtime*, L_Record*), L_Record** out);
bl_Status l_get(bl_Runtime* r, uint64_t id, unsigned kind, L_Record** out, bool disposedOK = false);
L_Record* l_peek(bl_Runtime* r, uint64_t id);
void l_pin(L_Record* p);
void l_unpin(L_Record* p);

#define L_TRY(call)                   \
    do                                \
    {                                 \
        bl_Status l_status_ = (call); \
        if (l_status_ != BL_OK)       \
        {                             \
            return l_status_;         \
        }                             \
    }                                 \
    while (0)
#define L_FAIL(r, status, message) l_error((r), (status), __func__, (message))
#define L_CALL(r, status, expression) \
    do                                \
    {                                 \
        l_enterHost(r);               \
        (status) = (expression);      \
        l_leaveHost(r);               \
    }                                 \
    while (0)
#define L_GET(handle, kind, type, variable)                                    \
    L_Record* variable##_record;                                               \
    L_TRY(l_get((handle)._runtime, (handle)._id, (kind), &variable##_record)); \
    type* variable = (type*)variable##_record
#define L_RETIRED(handle, kind, type, variable)                                      \
    L_Record* variable##_record;                                                     \
    L_TRY(l_get((handle)._runtime, (handle)._id, (kind), &variable##_record, true)); \
    type* variable = (type*)variable##_record
#define L_NEW(r, kind, priority, cleanup, type, variable)                                  \
    L_Record* variable##_record;                                                           \
    L_TRY(l_record((r), sizeof(type), (kind), (priority), (cleanup), &variable##_record)); \
    type* variable = (type*)variable##_record
#define L_NULL(handle) ((handle)._runtime == NULL && (handle)._id == 0)

#endif
