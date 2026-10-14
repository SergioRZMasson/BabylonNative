#include <babylon_lite.h>
#include <malloc.h>
#include <assert.h>

static void* allocate(void*,size_t bytes,size_t alignment)
{
    return _aligned_malloc(bytes,alignment);
}
static void release(void*,void* memory,size_t,size_t)
{
    _aligned_free(memory);
}
int main()
{
    bl_RuntimeOptions2 options = {};
    options.structSize=sizeof(options);
    options.version=BL_RUNTIME_OPTIONS_VERSION_2;
    options.base.allocator={NULL,allocate,release};
    bl_Runtime* runtime=NULL;
    assert(bl_createRuntime2(&options,&runtime)==BL_OK);
    assert(bl_disposeRuntime(runtime)==BL_OK);
}
