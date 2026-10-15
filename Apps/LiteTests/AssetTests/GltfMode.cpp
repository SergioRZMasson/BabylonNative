#include "RuntimeInternal.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <malloc.h>
#endif

static size_t live = 0;

static void* allocate(void*, size_t bytes, size_t alignment)
{
    void* result = NULL;
#if defined(_WIN32)
    result = _aligned_malloc(bytes, alignment);
#else
    if (posix_memalign(&result, alignment, bytes))
    {
        return NULL;
    }
#endif
    if (result)
    {
        ++live;
    }
    return result;
}

static void release(void*, void* memory, size_t, size_t)
{
    if (memory)
    {
        --live;
#if defined(_WIN32)
        _aligned_free(memory);
#else
        free(memory);
#endif
    }
}

static void require(bool value)
{
    if (!value)
    {
        abort();
    }
}

int main()
{
    bl_Runtime* runtime = NULL;
    bl_Runtime* other = NULL;
    bl_RuntimeOptions options = {};
    options.allocator = {NULL, allocate, release};
    require(bl_createRuntime(&options, &runtime) == BL_OK);
    require(bl_createRuntime(&options, &other) == BL_OK);
    require(runtime->gltfImportMode == BL_GLTF_IMPORT_SERVICE);
    require(bl_setGltfImportMode(runtime, (bl_GltfImportMode)37) == BL_INVALID_ARGUMENT);
    require(runtime->gltfImportMode == BL_GLTF_IMPORT_SERVICE);
#if defined(LITE_EXPECT_NATIVE_ENABLED)
    require(bl_setGltfImportMode(runtime, BL_GLTF_IMPORT_NATIVE) == BL_OK);
    require(runtime->gltfImportMode == BL_GLTF_IMPORT_NATIVE);
#else
    require(bl_setGltfImportMode(runtime, BL_GLTF_IMPORT_NATIVE) == BL_UNSUPPORTED);
    require(runtime->gltfImportMode == BL_GLTF_IMPORT_SERVICE);
#endif
    require(other->gltfImportMode == BL_GLTF_IMPORT_SERVICE);
    require(bl_setGltfImportMode(runtime, BL_GLTF_IMPORT_SERVICE) == BL_OK);
    require(bl_disposeRuntime(other) == BL_OK);
    require(bl_disposeRuntime(runtime) == BL_OK);
    require(live == 0);
    puts("Native mode selection and feature availability passed");
    return 0;
}
