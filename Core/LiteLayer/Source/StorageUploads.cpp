#include "LiteInternal.h"

bl_Status l_validateStorageUploadViews(L_Engine* engine, size_t count)
{
    size_t scenes = 0;
    for (L_Scene* scene = engine->scenes; scene; scene = scene->next)
    {
        ++scenes;
    }
    size_t cursor = l_inFrame() ? engine->mipViewCursor : engine->assetViewCount;
    size_t views = engine->native.target.viewCount;
    if (scenes > views || cursor > views - scenes || count > views - scenes - cursor)
    {
        return L_FAIL(engine->runtime, BL_UNSUPPORTED, "Insufficient buffer-upload views");
    }
    return BL_OK;
}

bl_Status l_prepareStorageUpload(const bgfx::BufferRegion* destination, const void* bytes,
                                 L_StorageUpload* upload)
{
    // NativeEngine StorageBuffer::Upload uses a fresh source and an ordered GPU copy.
    // An initialized, immutable source avoids the pinned D3D11 READ-only dynamic
    // buffer's raw-view/STAGING incompatibility. The destination remains persistent.
    bgfx::VertexLayout layout;
    layout.begin().skip(4).end();
    bgfx::VertexBufferHandle source =
        bgfx::createVertexBuffer(bgfx::copy(bytes, destination->size), layout);
    if (!bgfx::isValid(source))
    {
        return BL_OUT_OF_MEMORY;
    }
    upload->source = source;
    upload->destinationIndex = destination->handle.idx;
    upload->destinationType = destination->handle.type;
    upload->offset = destination->offset;
    upload->size = destination->size;
    return BL_OK;
}

void l_cancelStorageUploads(L_StorageUpload* uploads, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        bgfx::destroy(uploads[i].source);
    }
}

bl_Status l_submitStorageUploads(L_Engine* engine, L_StorageUpload* uploads, size_t count)
{
    if (!count)
    {
        return BL_OK;
    }
    bl_Status status = l_validateStorageUploadViews(engine, count);
    bool inFrame = l_inFrame();
    if (status == BL_OK && !inFrame)
    {
        status = l_retire(engine);
    }
    if (status == BL_OK)
    {
        uint16_t cursor = inFrame ? engine->mipViewCursor : engine->assetViewCount;
        for (size_t i = 0; i < count; ++i)
        {
            bgfx::BufferRegion source;
            source.init(uploads[i].source, 0, uploads[i].size);
            bgfx::BufferHandle target;
            target.idx = uploads[i].destinationIndex;
            target.type = uploads[i].destinationType;
            bgfx::BufferRegion destination;
            destination.init(target, uploads[i].offset, uploads[i].size);
            bgfx::blit((uint16_t)(engine->native.target.firstViewId + cursor++), destination,
                       source);
        }
        if (inFrame)
        {
            engine->mipViewCursor = cursor;
        }
        else
        {
            status = l_retire(engine);
        }
    }
    l_cancelStorageUploads(uploads, count);
    return status;
}
