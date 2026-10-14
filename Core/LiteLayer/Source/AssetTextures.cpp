#include "AssetInternal.h"

static bl_Status unsupportedTexture(const char** reason, const char* message)
{
    if (reason)
    {
        *reason = message;
    }
    return BL_UNSUPPORTED;
}

static bool descriptorSpan(const void* pointer, size_t count, size_t size, size_t alignment)
{
    size_t bytes;
    return count <= UINT32_MAX && l_size(count, size, &bytes) && l_allocationFits(bytes) &&
           l_typedSpan(pointer, count, alignment);
}

static bool wrapValid(bl_AssetOptionalIndex wrap)
{
    return !wrap.present || wrap.value == 10497 || wrap.value == 33071 || wrap.value == 33648;
}

bl_Status l_validateAssetTextures(const bl_ParsedGltf3* extended, const char** reason)
{
    const bl_ParsedGltf* data = &extended->document;
    if (!descriptorSpan(extended->images, data->imageCount, sizeof(bl_AssetImage),
                        alignof(bl_AssetImage)) ||
        !descriptorSpan(extended->textures, data->textureCount, sizeof(bl_AssetTexture),
                        alignof(bl_AssetTexture)) ||
        !descriptorSpan(extended->samplers, data->samplerCount, sizeof(bl_AssetSampler),
                        alignof(bl_AssetSampler)) ||
        !descriptorSpan(extended->materialInputs, data->materialCount,
                        sizeof(bl_AssetMaterialInputs), alignof(bl_AssetMaterialInputs)))
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < data->imageCount; ++i)
    {
        const bl_AssetImage* image = extended->images + i;
        if ((image->unsupportedFeatures & ~UINT64_C(15)) ||
            (image->hasMimeType && !l_string(image->mimeType)) ||
            (image->hasUri && (!l_string(image->uri) || !image->uri.length)) ||
            image->hasUri == image->bufferView.present ||
            (image->bufferView.present && image->bufferView.value >= data->bufferViewCount))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (image->unsupportedFeatures || image->hasUri)
        {
            return unsupportedTexture(
                reason, "Only embedded GLB bufferView images are supported; "
                        "external/data image URIs and image extensions are unsupported");
        }
        if (!image->hasMimeType)
        {
            return BL_INVALID_ARGUMENT;
        }
        if (!l_name(image->mimeType, "image/png") && !l_name(image->mimeType, "image/jpeg"))
        {
            return unsupportedTexture(reason,
                                      "Embedded images must be PNG or JPEG, not a compressed "
                                      "or extension-specific image format");
        }
        const bl_AssetBufferView* view = data->bufferViews + image->bufferView.value;
        if (!view->byteLength || view->byteStride.present)
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    for (size_t i = 0; i < data->textureCount; ++i)
    {
        const bl_AssetTexture* texture = extended->textures + i;
        if ((texture->unsupportedFeatures & ~UINT64_C(15)) ||
            (texture->source.present && texture->source.value >= data->imageCount) ||
            (texture->sampler.present && texture->sampler.value >= data->samplerCount))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (texture->unsupportedFeatures || !texture->source.present)
        {
            return unsupportedTexture(reason, "Texture source or texture extension is unsupported");
        }
    }
    for (size_t i = 0; i < data->samplerCount; ++i)
    {
        const bl_AssetSampler* sampler = extended->samplers + i;
        uint32_t min = sampler->minFilter.value;
        if ((sampler->magFilter.present && sampler->magFilter.value != 9728 &&
             sampler->magFilter.value != 9729) ||
            (sampler->minFilter.present && min != 9728 && min != 9729 &&
             (min < 9984 || min > 9987)) ||
            !wrapValid(sampler->wrapS) || !wrapValid(sampler->wrapT))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    for (size_t i = 0; i < data->materialCount; ++i)
    {
        const bl_AssetMaterialInputs* material = extended->materialInputs + i;
        const bl_AssetTextureInfo* inputs[5] = {&material->baseColor, &material->metallicRoughness,
                                                &material->normal, &material->occlusion,
                                                &material->emissive};
        uint32_t mask = 0;
        for (unsigned j = 0; j < 5; ++j)
        {
            const bl_AssetTextureInfo* input = inputs[j];
            if (input->unsupportedFeatures & ~UINT64_C(15))
            {
                return BL_INVALID_ARGUMENT;
            }
            if (!input->present)
            {
                continue;
            }
            mask |= 1u << j;
            if (input->index >= data->textureCount)
            {
                const char* messages[5] = {"Base-color texture index is out of range",
                                           "Metallic/roughness texture index is out of range",
                                           "Normal texture index is out of range",
                                           "Occlusion texture index is out of range",
                                           "Emissive texture index is out of range"};
                if (reason)
                {
                    *reason = messages[j];
                }
                return BL_INVALID_ARGUMENT;
            }
            if ((input->hasScale && !isfinite(input->scale)) ||
                (input->hasStrength &&
                 (!isfinite(input->strength) || input->strength < 0 || input->strength > 1)) ||
                (j != 2 && input->hasScale) || (j != 3 && input->hasStrength))
            {
                return BL_INVALID_ARGUMENT;
            }
            if (input->unsupportedFeatures ||
                (input->texCoord.present && input->texCoord.value > 1))
            {
                return unsupportedTexture(reason, "Texture extensions and UV channels beyond "
                                                  "TEXCOORD_0/1 are unsupported");
            }
        }
        if (mask != data->materials[i].textureInfoFields)
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    return BL_OK;
}

static bl_AddressMode addressMode(bl_AssetOptionalIndex value)
{
    return value.present && value.value == 33071   ? BL_ADDRESS_CLAMP_TO_EDGE
           : value.present && value.value == 33648 ? BL_ADDRESS_MIRROR_REPEAT
                                                   : BL_ADDRESS_REPEAT;
}

static bl_Status imageTexture(L_Asset* asset, const bl_ParsedGltf3* data,
                              const bl_AssetTextureInfo* input, bool srgb, bl_Texture2D* out,
                              const bl_ParsedGltf4* full)
{
    bl_Runtime* runtime = asset->engine->runtime;
    size_t cache = (size_t)input->index * 2 + (srgb ? 1 : 0);
    if (!L_NULL(asset->textures[cache]))
    {
        *out = asset->textures[cache];
        return BL_OK;
    }
    if (!runtime->hasIO || !runtime->io.decodeImage || !runtime->io.releaseImage)
    {
        return l_error(runtime, BL_UNSUPPORTED, __func__, "Host PNG/JPEG image service is required",
                       0);
    }
    const bl_AssetTexture* texture = data->textures + input->index;
    const bl_AssetImage* source = data->images + texture->source.value;
    const bl_AssetBufferView* view = data->document.bufferViews + source->bufferView.value;
    const uint8_t* bytes = data->document.buffers[view->buffer].bytes.data +
                           l_assetViewOffset(&data->document, source->bufferView.value, full);
    const bl_AssetSampler defaults = {};
    const bl_AssetSampler* sampler =
        texture->sampler.present ? data->samplers + texture->sampler.value : &defaults;
    uint32_t min = sampler->minFilter.present ? sampler->minFilter.value : 9987;
    bool mips = min >= 9984;
    bool pointMin = min == 9728 || min == 9984 || min == 9986;
    bool pointMag = sampler->magFilter.present && sampler->magFilter.value == 9728;
    bl_PixelsTexture2DOptions options = {addressMode(sampler->wrapS), addressMode(sampler->wrapT),
                                         pointMin ? BL_FILTER_NEAREST : BL_FILTER_LINEAR,
                                         pointMag ? BL_FILTER_NEAREST : BL_FILTER_LINEAR, srgb};
    bl_HostImage image = {};
    bl_Status status;
    L_CALL(runtime, status,
           runtime->io.decodeImage(runtime->io.userData, {bytes, view->byteLength}, &image));
    size_t rowBytes = 0;
    size_t byteCount = 0;
    size_t lastRow = 0;
    uint8_t* tight = NULL;
    if (status == BL_OK)
    {
        if (!image.rgba8 || !image.width || !image.height || image.width > UINT16_MAX ||
            image.height > UINT16_MAX || !l_size(image.width, 4, &rowBytes) ||
            image.rowStrideBytes < rowBytes || !l_size(rowBytes, image.height, &byteCount) ||
            byteCount > UINT32_MAX || !l_size(image.rowStrideBytes, image.height - 1, &lastRow) ||
            lastRow > SIZE_MAX - rowBytes ||
            (uintptr_t)image.rgba8 > UINTPTR_MAX - (lastRow + rowBytes))
        {
            status = BL_INVALID_ARGUMENT;
        }
        else
        {
            tight = (uint8_t*)l_alloc(runtime, byteCount);
            if (!tight)
            {
                status = BL_OUT_OF_MEMORY;
            }
        }
    }
    if (status == BL_OK)
    {
        for (uint32_t y = 0; y < image.height; ++y)
        {
            memcpy(tight + y * rowBytes, image.rgba8 + y * image.rowStrideBytes, rowBytes);
        }
        status = l_createAssetPixelTexture({runtime, asset->engine->record.id}, {tight, byteCount},
                                           image.width, image.height, &options, true, out);
    }
    l_free(runtime, tight);
    l_enterHost(runtime);
    runtime->io.releaseImage(runtime->io.userData, &image);
    l_leaveHost(runtime);
    if (status == BL_OK)
    {
        L_Texture* record = (L_Texture*)l_peek(runtime, out->_id);
        if (min == 9984 || min == 9985)
        {
            record->flags |= BGFX_SAMPLER_MIP_POINT;
        }
        record->samplerMaxLod = mips ? 128 : 0;
        asset->textures[cache] = *out;
    }
    return status;
}

static void replaceMaterialTexture(bl_Runtime* runtime, bl_Texture2D* slot, bl_Texture2D texture)
{
    bl_Texture2D old = *slot;
    L_Texture* previous = (L_Texture*)l_peek(runtime, old._id);
    if (previous && previous->references)
    {
        --previous->references;
    }
    ++((L_Texture*)l_peek(runtime, texture._id))->references;
    *slot = texture;
    if (!L_NULL(old))
    {
        bl_disposeTexture2D(old);
    }
}

bl_Status l_applyAssetMaterialInputs(L_Asset* asset, const bl_ParsedGltf3* data, uint32_t index,
                                     bl_Material handle, const bl_ParsedGltf4* full)
{
    bl_Runtime* runtime = asset->engine->runtime;
    L_Material* material = (L_Material*)l_peek(runtime, handle._id);
    const bl_AssetMaterialInputs* inputs = data->materialInputs + index;
    const bl_AssetPbrDefinition* definition = data->document.materials + index;
    if (inputs->baseColor.present)
    {
        bl_Texture2D texture = {};
        L_TRY(imageTexture(asset, data, &inputs->baseColor, true, &texture, full));
        replaceMaterialTexture(runtime, &material->pbrInfo.baseColorTexture, texture);
        material->pbrBaseImage = true;
        material->pbrBaseUv1 =
            inputs->baseColor.texCoord.present && inputs->baseColor.texCoord.value == 1;
        material->pbrInfo.hasBaseColorFactor = definition->hasBaseColorFactor;
        material->pbrInfo.baseColorFactor =
            definition->hasBaseColorFactor ? definition->baseColorFactor : bl_Color4{1, 1, 1, 1};
        // Authored image alpha receives its factor in WGSL, exactly once.
        material->pbrInfo.alpha = 1;
        const bl_Color4* factor = &material->pbrInfo.baseColorFactor;
        material->pbrInfo.hasBaseColorFactor =
            factor->r != 1 || factor->g != 1 || factor->b != 1 || factor->a != 1;
    }
    if (inputs->metallicRoughness.present)
    {
        bl_Texture2D texture = {};
        L_TRY(imageTexture(asset, data, &inputs->metallicRoughness, false, &texture, full));
        replaceMaterialTexture(runtime, &material->pbrInfo.ormTexture, texture);
        material->pbrOrmImage = true;
        material->pbrOrmUv1 = inputs->metallicRoughness.texCoord.present &&
                              inputs->metallicRoughness.texCoord.value == 1;
        material->pbrInfo.metallicFactor =
            definition->hasMetallicFactor ? definition->metallicFactor : 1;
        material->pbrInfo.roughnessFactor =
            definition->hasRoughnessFactor ? definition->roughnessFactor : 1;
    }
    const bl_AssetTextureInfo* roles[3] = {&inputs->normal, &inputs->occlusion, &inputs->emissive};
    bl_Texture2D* slots[3] = {&material->pbrNormalTexture, &material->pbrOcclusionTexture,
                              &material->pbrEmissiveTexture};
    bool* uv1[3] = {&material->pbrNormalUv1, &material->pbrOcclusionUv1, &material->pbrEmissiveUv1};
    for (unsigned i = 0; i < 3; ++i)
    {
        if (roles[i]->present)
        {
            bl_Texture2D texture = {};
            L_TRY(imageTexture(asset, data, roles[i], i == 2, &texture, full));
            replaceMaterialTexture(runtime, slots[i], texture);
            *uv1[i] = roles[i]->texCoord.present && roles[i]->texCoord.value == 1;
            material->pbrExtra = true;
        }
    }
    material->pbrNormalScale =
        inputs->normal.present && inputs->normal.hasScale ? inputs->normal.scale : 1;
    material->pbrInfo.occlusionStrength =
        inputs->occlusion.present ? (inputs->occlusion.hasStrength ? inputs->occlusion.strength : 1)
                                  : 0;
    return BL_OK;
}
