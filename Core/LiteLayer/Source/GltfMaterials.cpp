#include "GltfInternal.h"

static void* allocateImage(void* runtime, cgltf_size bytes)
{
    return l_alloc((bl_Runtime*)runtime, bytes ? bytes : 1);
}

static void freeImage(void* runtime, void* memory)
{
    l_free((bl_Runtime*)runtime, memory);
}

static bl_AddressMode address(cgltf_wrap_mode value)
{
    return value == cgltf_wrap_mode_clamp_to_edge     ? BL_ADDRESS_CLAMP_TO_EDGE
           : value == cgltf_wrap_mode_mirrored_repeat ? BL_ADDRESS_MIRROR_REPEAT
                                                      : BL_ADDRESS_REPEAT;
}

static bl_Status imageBytes(L_Gltf* gltf, const cgltf_image* source, bl_Bytes* out, uint8_t** owned)
{
    if (!source || source->extensions_count || (source->buffer_view && source->uri))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (source->mime_type && strcmp(source->mime_type, "image/png") &&
        strcmp(source->mime_type, "image/jpeg"))
    {
        gltf->reason = "Image format is not PNG or JPEG";
        return BL_UNSUPPORTED;
    }
    if (source->buffer_view)
    {
        if (source->buffer_view->stride || !source->buffer_view->size)
        {
            return BL_INVALID_ARGUMENT;
        }
        *out = {cgltf_buffer_view_data(source->buffer_view), source->buffer_view->size};
        return BL_OK;
    }
    if (!source->uri)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!strncmp(source->uri, "data:", 5))
    {
        const char* comma = strchr(source->uri, ',');
        if (!comma || comma - source->uri < 7 || strncmp(comma - 7, ";base64", 7))
        {
            return BL_UNSUPPORTED;
        }
        size_t length = strlen(comma + 1);
        if (!length || length % 4)
        {
            return BL_INVALID_ARGUMENT;
        }
        size_t bytes = length / 4 * 3;
        bytes -= comma[length] == '=' ? 1 : 0;
        bytes -= comma[length - 1] == '=' ? 1 : 0;
        void* memory = NULL;
        cgltf_options options = {};
        options.memory = {allocateImage, freeImage, gltf->runtime};
        L_TRY(l_gltfResult(gltf, cgltf_load_buffer_base64(&options, bytes, comma + 1, &memory)));
        *owned = (uint8_t*)memory;
        *out = {*owned, bytes};
        return BL_OK;
    }
    size_t size = 0;
    L_TRY(l_gltfReadUri(gltf, source->uri, owned, &size));
    *out = {*owned, size};
    return BL_OK;
}

static bl_Status texture(L_Gltf* gltf, L_Asset* asset, const cgltf_texture_view* view, bool srgb,
                         bl_Texture2D* out)
{
    const cgltf_texture* source = view->texture;
    if (view->has_transform || view->texcoord < 0 || view->texcoord > 1 || source->has_basisu ||
        source->has_webp || source->extensions_count)
    {
        gltf->reason =
            "Texture transforms, extended formats or UV channels beyond 0/1 are unsupported";
        return BL_UNSUPPORTED;
    }
    size_t cache = (size_t)(source - gltf->data->textures) * 2 + (srgb ? 1 : 0);
    if (!L_NULL(asset->textures[cache]))
    {
        *out = asset->textures[cache];
        return BL_OK;
    }
    bl_Runtime* runtime = gltf->runtime;
    if (!runtime->hasIO || !runtime->io.decodeImage || !runtime->io.releaseImage)
    {
        gltf->reason = "Host image decoding service is required";
        return BL_UNSUPPORTED;
    }
    cgltf_sampler defaults = {};
    defaults.wrap_s = cgltf_wrap_mode_repeat;
    defaults.wrap_t = cgltf_wrap_mode_repeat;
    const cgltf_sampler* sampler = source->sampler ? source->sampler : &defaults;
    int min = sampler->min_filter ? sampler->min_filter : 9987;
    if ((min != 9728 && min != 9729 && (min < 9984 || min > 9987)) ||
        (sampler->mag_filter && sampler->mag_filter != 9728 && sampler->mag_filter != 9729) ||
        (sampler->wrap_s != 33071 && sampler->wrap_s != 33648 && sampler->wrap_s != 10497) ||
        (sampler->wrap_t != 33071 && sampler->wrap_t != 33648 && sampler->wrap_t != 10497) ||
        sampler->extensions_count)
    {
        return BL_INVALID_ARGUMENT;
    }
    uint8_t* owned = NULL;
    bl_Bytes encoded = {};
    bl_Status status = imageBytes(gltf, source->image, &encoded, &owned);
    if (status != BL_OK)
    {
        l_free(runtime, owned);
        return status;
    }
    bl_HostImage image = {};
    L_CALL(runtime, status, runtime->io.decodeImage(runtime->io.userData, encoded, &image));
    size_t rowBytes = 0;
    size_t bytes = 0;
    size_t last = 0;
    uint8_t* pixels = NULL;
    if (status == BL_OK &&
        (!image.rgba8 || !image.width || !image.height || image.width > UINT16_MAX ||
         image.height > UINT16_MAX || !l_size(image.width, 4, &rowBytes) ||
         image.rowStrideBytes < rowBytes || !l_size(rowBytes, image.height, &bytes) ||
         bytes > UINT32_MAX || !l_size(image.rowStrideBytes, image.height - 1, &last) ||
         last > SIZE_MAX - rowBytes || (uintptr_t)image.rgba8 > UINTPTR_MAX - last - rowBytes))
    {
        status = BL_INVALID_ARGUMENT;
    }
    if (status == BL_OK)
    {
        pixels = (uint8_t*)l_alloc(runtime, bytes);
        if (!pixels)
        {
            status = BL_OUT_OF_MEMORY;
        }
    }
    if (status == BL_OK)
    {
        for (uint32_t y = 0; y < image.height; ++y)
        {
            memcpy(pixels + y * rowBytes, image.rgba8 + y * image.rowStrideBytes, rowBytes);
        }
        bool point = min == 9728 || min == 9984 || min == 9986;
        bl_PixelsTexture2DOptions options = {
            address(sampler->wrap_s), address(sampler->wrap_t),
            point ? BL_FILTER_NEAREST : BL_FILTER_LINEAR,
            sampler->mag_filter == 9728 ? BL_FILTER_NEAREST : BL_FILTER_LINEAR, srgb};
        status = l_createAssetPixelTexture({runtime, asset->engine->record.id}, {pixels, bytes},
                                           image.width, image.height, &options, true, out);
    }
    l_free(runtime, pixels);
    l_enterHost(runtime);
    runtime->io.releaseImage(runtime->io.userData, &image);
    l_leaveHost(runtime);
    l_free(runtime, owned);
    if (status == BL_OK)
    {
        L_Texture* record = (L_Texture*)l_peek(runtime, out->_id);
        if (min == 9984 || min == 9985)
        {
            record->flags |= BGFX_SAMPLER_MIP_POINT;
        }
        record->samplerMaxLod = min >= 9984 ? 128 : 0;
        asset->textures[cache] = *out;
    }
    else
    {
        gltf->reason = "Image decoding or native texture creation failed";
    }
    return status;
}

static bool factor(float value)
{
    return isfinite(value) && value >= 0 && value <= 1;
}

static void replace(bl_Runtime* runtime, bl_Texture2D* slot, bl_Texture2D value)
{
    bl_Texture2D previous = *slot;
    L_Texture* old = (L_Texture*)l_peek(runtime, previous._id);
    if (old && old->references)
    {
        --old->references;
    }
    ++((L_Texture*)l_peek(runtime, value._id))->references;
    *slot = value;
    if (!L_NULL(previous))
    {
        bl_disposeTexture2D(previous);
    }
}

bl_Status l_gltfMaterial(L_Gltf* gltf, L_Asset* asset, const cgltf_material* source,
                         bl_Material* out)
{
    size_t index = source ? (size_t)(source - gltf->data->materials) + 1 : 0;
    if (!L_NULL(asset->materials[index]))
    {
        *out = asset->materials[index];
        return BL_OK;
    }
    bl_Runtime* runtime = gltf->runtime;
    bl_AssetPbrDefinition definition = {};
    bl_AssetSpecularGlossinessInputs workflow = {};
    if (source)
    {
        if (source->has_clearcoat || source->has_transmission || source->has_volume ||
            source->has_ior || source->has_specular || source->has_sheen ||
            source->has_emissive_strength || source->has_iridescence ||
            source->has_diffuse_transmission || source->has_anisotropy || source->has_dispersion ||
            source->unlit || source->extensions_count)
        {
            gltf->reason = "Material extension beyond specular/glossiness is not implemented";
            return BL_UNSUPPORTED;
        }
        const cgltf_pbr_metallic_roughness* mr = &source->pbr_metallic_roughness;
        const cgltf_pbr_specular_glossiness* sg = &source->pbr_specular_glossiness;
        definition.hasName = source->name != NULL;
        definition.name = {source->name, source->name ? strlen(source->name) : 0};
        definition.hasBaseColorFactor = true;
        definition.baseColorFactor = {mr->base_color_factor[0], mr->base_color_factor[1],
                                      mr->base_color_factor[2], mr->base_color_factor[3]};
        definition.hasMetallicFactor = true;
        definition.metallicFactor = mr->metallic_factor;
        definition.hasRoughnessFactor = true;
        definition.roughnessFactor = mr->roughness_factor;
        definition.hasEmissiveFactor = true;
        definition.emissiveFactor = {source->emissive_factor[0], source->emissive_factor[1],
                                     source->emissive_factor[2]};
        definition.hasAlphaMode = true;
        definition.alphaMode = source->alpha_mode == cgltf_alpha_mode_blend ? BL_ASSET_ALPHA_BLEND
                               : source->alpha_mode == cgltf_alpha_mode_mask
                                   ? BL_ASSET_ALPHA_MASK
                                   : BL_ASSET_ALPHA_OPAQUE;
        definition.hasAlphaCutoff = true;
        definition.alphaCutoff = source->alpha_cutoff;
        definition.hasDoubleSided = true;
        definition.doubleSided = source->double_sided != 0;
        workflow.present = source->has_pbr_specular_glossiness != 0;
        workflow.hasDiffuseFactor = true;
        workflow.diffuseFactor = {sg->diffuse_factor[0], sg->diffuse_factor[1],
                                  sg->diffuse_factor[2], sg->diffuse_factor[3]};
        workflow.hasSpecularFactor = true;
        workflow.specularFactor = {sg->specular_factor[0], sg->specular_factor[1],
                                   sg->specular_factor[2]};
        workflow.hasGlossinessFactor = true;
        workflow.glossinessFactor = sg->glossiness_factor;
        for (size_t i = 0; i < 4; ++i)
        {
            if (!factor(mr->base_color_factor[i]) ||
                (workflow.present && !factor(sg->diffuse_factor[i])))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t i = 0; i < 3; ++i)
        {
            if (!isfinite(source->emissive_factor[i]) || source->emissive_factor[i] < 0 ||
                (workflow.present && !factor(sg->specular_factor[i])))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        if (!factor(mr->metallic_factor) || !factor(mr->roughness_factor) ||
            (workflow.present && !factor(sg->glossiness_factor)) ||
            !isfinite(source->alpha_cutoff) || source->alpha_cutoff < 0 ||
            !isfinite(source->normal_texture.scale) || !factor(source->occlusion_texture.scale))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    bl_Material handle = {};
    L_TRY(l_createPbrFactorMaterial({runtime, asset->engine->record.id},
                                    source ? &definition : NULL, &handle, &workflow));
    asset->materials[index] = handle;
    l_pin(l_peek(runtime, handle._id));
    L_Material* material = (L_Material*)l_peek(runtime, handle._id);
    if (source)
    {
        const cgltf_texture_view* views[5] = {
            workflow.present ? &source->pbr_specular_glossiness.diffuse_texture
                             : &source->pbr_metallic_roughness.base_color_texture,
            workflow.present ? &source->pbr_specular_glossiness.specular_glossiness_texture
                             : &source->pbr_metallic_roughness.metallic_roughness_texture,
            &source->normal_texture, &source->occlusion_texture, &source->emissive_texture};
        bl_Texture2D* slots[5] = {&material->pbrInfo.baseColorTexture,
                                  workflow.present ? &material->pbrSpecularGlossinessTexture
                                                   : &material->pbrInfo.ormTexture,
                                  &material->pbrNormalTexture, &material->pbrOcclusionTexture,
                                  &material->pbrEmissiveTexture};
        bool* uv1[5] = {
            &material->pbrBaseUv1,
            workflow.present ? &material->pbrSpecularGlossinessUv1 : &material->pbrOrmUv1,
            &material->pbrNormalUv1, &material->pbrOcclusionUv1, &material->pbrEmissiveUv1};
        for (size_t i = 0; i < 5; ++i)
        {
            if (views[i]->texture)
            {
                bl_Texture2D value = {};
                L_TRY(texture(gltf, asset, views[i],
                              i == 0 || i == 4 || (i == 1 && workflow.present), &value));
                replace(runtime, slots[i], value);
                *uv1[i] = views[i]->texcoord == 1;
                material->pbrExtra |= i >= 2;
            }
        }
        material->pbrBaseImage = views[0]->texture != NULL;
        material->pbrOrmImage = !workflow.present && views[1]->texture != NULL;
        if (!workflow.present && material->pbrBaseImage)
        {
            material->pbrInfo.baseColorFactor = definition.baseColorFactor;
            const bl_Color4* value = &definition.baseColorFactor;
            material->pbrInfo.hasBaseColorFactor =
                value->r != 1 || value->g != 1 || value->b != 1 || value->a != 1;
            material->pbrInfo.alpha = 1;
        }
        if (material->pbrOrmImage)
        {
            material->pbrInfo.metallicFactor = definition.metallicFactor;
            material->pbrInfo.roughnessFactor = definition.roughnessFactor;
        }
        material->pbrNormalScale = source->normal_texture.scale;
        material->pbrInfo.occlusionStrength =
            views[3]->texture ? source->occlusion_texture.scale : 0;
    }
    *out = handle;
    return BL_OK;
}
