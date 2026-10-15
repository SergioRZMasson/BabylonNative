#include "AssetMotionInternal.h"

static void normalized(float* out, const float* in)
{
    double length = sqrt((double)in[0] * in[0] + (double)in[1] * in[1] + (double)in[2] * in[2]);
    for (size_t i = 0; i < 3; ++i)
    {
        out[i] = length > 0 ? (float)(in[i] / length) : 0;
    }
}

void l_freeAssetDeformation(bl_Runtime* runtime, L_AssetDeformation* d)
{
    if (d->mesh)
    {
        d->mesh->skin = NULL;
    }
    if (bgfx::isValid(d->bitangentTexture))
    {
        bgfx::destroy(d->bitangentTexture);
    }
    for (size_t i = 0; i < 3; ++i)
    {
        l_free(runtime, d->bind[i]);
        l_free(runtime, d->output[i]);
    }
    l_free(runtime, d->joints);
    l_free(runtime, d->weights);
    l_free(runtime, d->packed);
    l_free(runtime, d->bitangents);
    l_free(runtime, d->gpuBitangents);
    l_free(runtime, d);
}

void l_detachAssetSkin(L_Mesh* mesh)
{
    if (mesh->skin)
    {
        L_AssetDeformation* d = mesh->skin;
        if (bgfx::isValid(d->bitangentTexture))
        {
            bgfx::destroy(d->bitangentTexture);
            d->bitangentTexture.idx = BL_INVALID_BGFX_HANDLE;
        }
        d->mesh = NULL;
        mesh->skin = NULL;
    }
}

bl_Status l_bindAssetSkin(L_Asset* asset, const bl_ParsedGltf6* full, uint32_t node,
                          const bl_AssetPrimitive* primitive, L_Mesh* mesh)
{
    const bl_ParsedGltf* data = &full->document5.document4.document3.document;
    if (!data->nodes[node].skin.present)
    {
        return BL_OK;
    }
    bl_Runtime* runtime = asset->engine->runtime;
    L_AssetDeformation* d = (L_AssetDeformation*)l_alloc(runtime, sizeof(*d));
    if (!d)
    {
        return BL_OUT_OF_MEMORY;
    }
    d->bitangentTexture.idx = BL_INVALID_BGFX_HANDLE;
    d->asset = asset;
    d->mesh = mesh;
    d->skin = asset->motion->skins + data->nodes[node].skin.value;
    d->next = asset->motion->deformations;
    asset->motion->deformations = d;
    mesh->skin = d;
    d->gpuDirty = true;
    L_Geometry* g = &mesh->geometry;
    size_t sets = 0;
    for (size_t i = 0; i < primitive->attributeCount; ++i)
    {
        bool joint = false;
        if (l_assetInfluence(primitive->attributes[i].semantic, &joint) >= 0 && joint)
        {
            ++sets;
        }
    }
    size_t influences;
    size_t bytes;
    if (!l_size(sets, 4, &d->influences) || !l_size(g->vertices, d->influences, &influences) ||
        !l_size(influences, sizeof(float), &bytes) || bytes > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    d->joints = (uint32_t*)l_alloc(runtime, bytes);
    d->weights = (float*)l_alloc(runtime, bytes);
    d->packed = (uint8_t*)l_alloc(runtime, g->vertexCapacity * g->stride);
    if (!d->joints || !d->weights || !d->packed)
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < primitive->attributeCount; ++i)
    {
        const bl_AssetAttribute* attribute = primitive->attributes + i;
        bool joint = false;
        int set = l_assetInfluence(attribute->semantic, &joint);
        if (set < 0)
        {
            continue;
        }
        L_AssetAccessorReader reader;
        L_TRY(
            l_assetAccessorReader(data, attribute->accessor, &full->document5.document4, &reader));
        for (size_t v = 0; v < g->vertices; ++v)
        {
            double values[4];
            L_TRY(l_assetAccessorElement(&reader, v, values, !joint));
            for (size_t lane = 0; lane < 4; ++lane)
            {
                size_t index = v * d->influences + (size_t)set * 4 + lane;
                if (joint)
                {
                    d->joints[index] = (uint32_t)values[lane];
                }
                else
                {
                    d->weights[index] = (float)values[lane];
                }
            }
        }
    }
    for (size_t v = 0; v < g->vertices; ++v)
    {
        double total = 0;
        for (size_t i = 0; i < d->influences; ++i)
        {
            total += d->weights[v * d->influences + i];
        }
        for (size_t i = 0; i < d->influences; ++i)
        {
            size_t index = v * d->influences + i;
            d->weights[index] = (float)(d->weights[index] / total);
        }
    }
    const unsigned streams[3] = {0, 1, 4};
    for (size_t i = 0; i < 3; ++i)
    {
        if (!g->streams[streams[i]])
        {
            continue;
        }
        size_t components = i == 2 ? 4 : 3;
        bytes = g->vertices * components * sizeof(float);
        d->bind[i] = (float*)l_alloc(runtime, bytes);
        d->output[i] = (float*)l_alloc(runtime, bytes);
        if (!d->bind[i] || !d->output[i])
        {
            return BL_OUT_OF_MEMORY;
        }
        memcpy(d->bind[i], g->streams[streams[i]], bytes);
        if (i > 0)
        {
            for (size_t v = 0; v < g->vertices; ++v)
            {
                normalized(d->bind[i] + v * components, g->streams[streams[i]] + v * components);
            }
        }
    }
    bl_Material identity = l_meshMaterial(mesh);
    L_Material* material = (L_Material*)l_peek(runtime, identity._id);
    bool bitangent =
        !mesh->assetFlatNormal && d->bind[2] && material && !L_NULL(material->pbrNormalTexture);
    if (bitangent)
    {
        const bgfx::Caps* caps = bgfx::getCaps();
        uint32_t needed = BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_VERTEX;
        if ((caps->formats[bgfx::TextureFormat::RGBA32F] & needed) != needed ||
            !caps->limits.maxTextureSize || !g->vertices)
        {
            return BL_UNSUPPORTED;
        }
        size_t width =
            g->vertices < caps->limits.maxTextureSize ? g->vertices : caps->limits.maxTextureSize;
        size_t height = (g->vertices + width - 1) / width;
        if (height > caps->limits.maxTextureSize || width > UINT16_MAX || height > UINT16_MAX ||
            !l_size(width * height, 16, &bytes) || bytes > UINT32_MAX)
        {
            return BL_UNSUPPORTED;
        }
        d->textureWidth = (uint16_t)width;
        d->textureHeight = (uint16_t)height;
        d->bitangents = (float*)l_alloc(runtime, bytes);
        d->gpuBitangents = (float*)l_alloc(runtime, bytes);
        if (!d->bitangents || !d->gpuBitangents)
        {
            return BL_OUT_OF_MEMORY;
        }
        // Initial-data D3D11 textures are immutable on this pin.
        d->bitangentTexture = bgfx::createTexture2D(d->textureWidth, d->textureHeight, false, 1,
                                                    bgfx::TextureFormat::RGBA32F, 0);
        if (!bgfx::isValid(d->bitangentTexture))
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    return BL_OK;
}

bl_Status l_prepareAssetDeformation(L_Asset* asset)
{
    L_AssetMotion* motion = asset->motion;
    for (size_t i = 0; i < motion->skinCount; ++i)
    {
        L_AssetSkinData* skin = motion->skins + i;
        for (size_t j = 0; j < skin->count; ++j)
        {
            bl_Mat4 inverseBind;
            for (size_t c = 0; c < 16; ++c)
            {
                inverseBind.values[c] = skin->inverseBind[j * 16 + c];
            }
            bl_Mat4 palette;
            l_multiply(motion->worlds + skin->joints[j], &inverseBind, &palette, false);
            for (size_t c = 0; c < 16; ++c)
            {
                if (!isfinite(palette.values[c]))
                {
                    return BL_INVALID_ARGUMENT;
                }
                skin->palette[j * 16 + c] = (float)palette.values[c];
            }
        }
    }
    for (L_AssetDeformation* d = motion->deformations; d; d = d->next)
    {
        if (!d->mesh)
        {
            continue;
        }
        L_Geometry* g = &d->mesh->geometry;
        memcpy(d->packed, g->gpuVertices, g->vertexCapacity * g->stride);
        for (size_t v = 0; v < g->vertices; ++v)
        {
            float matrix[16] = {};
            for (size_t i = 0; i < d->influences; ++i)
            {
                size_t index = v * d->influences + i;
                const float* palette = d->skin->palette + (size_t)d->joints[index] * 16;
                for (size_t c = 0; c < 16; ++c)
                {
                    matrix[c] = (float)(matrix[c] + (double)palette[c] * d->weights[index]);
                }
            }
            for (size_t stream = 0; stream < 3; ++stream)
            {
                if (!d->bind[stream])
                {
                    continue;
                }
                size_t components = stream == 2 ? 4 : 3;
                const float* source = d->bind[stream] + v * components;
                float* output = d->output[stream] + v * components;
                for (size_t c = 0; c < 3; ++c)
                {
                    double value =
                        (double)matrix[c] * source[0] + (double)matrix[c + 4] * source[1] +
                        (double)matrix[c + 8] * source[2] + (stream == 0 ? matrix[c + 12] : 0);
                    output[c] = (float)value;
                    if (!isfinite(output[c]))
                    {
                        return BL_INVALID_ARGUMENT;
                    }
                }
                if (stream == 2)
                {
                    output[3] = source[3];
                }
                unsigned slot = stream == 2 ? 4 : (unsigned)stream;
                memcpy(d->packed + v * g->stride + g->offsets[slot], output,
                       components * sizeof(float));
            }
            if (d->bitangents)
            {
                const float* n = d->bind[1] + v * 3;
                const float* t = d->bind[2] + v * 4;
                double b[3] = {(n[1] * (double)t[2] - n[2] * (double)t[1]) * t[3],
                               (n[2] * (double)t[0] - n[0] * (double)t[2]) * t[3],
                               (n[0] * (double)t[1] - n[1] * (double)t[0]) * t[3]};
                for (size_t c = 0; c < 3; ++c)
                {
                    float value =
                        (float)(matrix[c] * b[0] + matrix[c + 4] * b[1] + matrix[c + 8] * b[2]);
                    if (!isfinite(value))
                    {
                        return BL_INVALID_ARGUMENT;
                    }
                    d->bitangents[v * 4 + c] = value;
                }
            }
        }
    }
    return BL_OK;
}

void l_commitAssetDeformation(L_Asset* asset)
{
    for (L_AssetDeformation* d = asset->motion->deformations; d; d = d->next)
    {
        if (!d->mesh)
        {
            continue;
        }
        L_Geometry* g = &d->mesh->geometry;
        size_t vertexBytes = g->vertices * g->stride;
        size_t textureBytes = (size_t)d->textureWidth * d->textureHeight * 16;
        bool vertexDirty = d->gpuDirty || memcmp(g->gpuVertices, d->packed, vertexBytes);
        bool textureDirty =
            d->bitangents && (d->gpuDirty || memcmp(d->bitangents, d->gpuBitangents, textureBytes));
        if (vertexDirty)
        {
            bgfx::update(g->vertexBuffer, 0, bgfx::copy(d->packed, (uint32_t)vertexBytes));
            memcpy(g->gpuVertices, d->packed, vertexBytes);
        }
        if (textureDirty)
        {
            bgfx::updateTexture2D(d->bitangentTexture, 0, 0, 0, 0, d->textureWidth,
                                  d->textureHeight,
                                  bgfx::copy(d->bitangents, (uint32_t)textureBytes));
            memcpy(d->gpuBitangents, d->bitangents, textureBytes);
        }
        d->gpuDirty = false;
        for (size_t i = 0; i < 3; ++i)
        {
            if (d->output[i])
            {
                memcpy(g->streams[i == 2 ? 4 : i], d->output[i],
                       g->vertices * (i == 2 ? 4 : 3) * sizeof(float));
            }
        }
        g->minimum = g->maximum = {};
        for (size_t v = 0; v < g->vertices; ++v)
        {
            const float* p = d->output[0] + v * 3;
            if (!v)
            {
                g->minimum = g->maximum = {p[0], p[1], p[2]};
            }
            g->minimum.x = p[0] < g->minimum.x ? p[0] : g->minimum.x;
            g->minimum.y = p[1] < g->minimum.y ? p[1] : g->minimum.y;
            g->minimum.z = p[2] < g->minimum.z ? p[2] : g->minimum.z;
            g->maximum.x = p[0] > g->maximum.x ? p[0] : g->maximum.x;
            g->maximum.y = p[1] > g->maximum.y ? p[1] : g->maximum.y;
            g->maximum.z = p[2] > g->maximum.z ? p[2] : g->maximum.z;
        }
    }
}
