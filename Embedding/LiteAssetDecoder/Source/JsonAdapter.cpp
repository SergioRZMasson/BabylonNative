#include <babylon_lite_asset_decoder.h>
#include <json.hpp>
#include <limits.h>
#include <math.h>
#include <new>
#include <stdint.h>
#include <string.h>

struct bl_AssetDecoder
{
    bl_Allocator allocator;
};

struct J_Block
{
    J_Block* next;
    size_t bytes;
};

struct J_Result
{
    bl_Allocator allocator;
    J_Block* blocks;
};

struct J_Failure
{
    const char* message;
    uint32_t code;
    bl_Status status;
};

using Json = nlohmann::json;

static void fail(const char* message, uint32_t code = 0, bl_Status status = BL_INVALID_ARGUMENT)
{
    throw J_Failure{message, code, status};
}

static void* allocate(J_Result* result, size_t count, size_t size)
{
    if (size && count > (SIZE_MAX - sizeof(J_Block)) / size)
    {
        fail("Descriptor allocation overflow");
    }
    size_t bytes = sizeof(J_Block) + count * size;
    J_Block* block =
        (J_Block*)result->allocator.allocate(result->allocator.userData, bytes, 16);
    if (!block)
    {
        throw std::bad_alloc();
    }
    if ((uintptr_t)block & 15)
    {
        result->allocator.deallocate(result->allocator.userData, block, bytes, 16);
        fail("Decoder allocator returned misaligned storage");
    }
    memset(block, 0, bytes);
    block->bytes = bytes;
    block->next = result->blocks;
    result->blocks = block;
    return block + 1;
}

static J_Result* createResult(bl_AssetDecoder* decoder)
{
    J_Result* result =
        (J_Result*)decoder->allocator.allocate(decoder->allocator.userData, sizeof(J_Result), 16);
    if (!result)
    {
        throw std::bad_alloc();
    }
    if ((uintptr_t)result & 15)
    {
        decoder->allocator.deallocate(decoder->allocator.userData, result, sizeof(J_Result), 16);
        fail("Decoder allocator returned misaligned result");
    }
    memset(result, 0, sizeof(*result));
    result->allocator = decoder->allocator;
    return result;
}

static void freeResult(void* token)
{
    J_Result* result = (J_Result*)token;
    if (!result)
    {
        return;
    }
    bl_Allocator allocator = result->allocator;
    J_Block* block = result->blocks;
    while (block)
    {
        J_Block* next = block->next;
        allocator.deallocate(allocator.userData, block, block->bytes, 16);
        block = next;
    }
    allocator.deallocate(allocator.userData, result, sizeof(*result), 16);
}

static bl_String copyString(J_Result* result, const std::string& value)
{
    if (value.find('\0') != std::string::npos)
    {
        fail("Embedded NUL in raw schema string");
    }
    char* bytes = (char*)allocate(result, value.size(), 1);
    memcpy(bytes, value.data(), value.size());
    return {bytes, value.size()};
}

static bl_String string(J_Result* result, const Json& value)
{
    if (!value.is_string())
    {
        fail("Expected schema string");
    }
    return copyString(result, value.get_ref<const std::string&>());
}

static uint64_t integer(const Json& value, uint64_t maximum = SIZE_MAX)
{
    if (!value.is_number_integer() && !value.is_number_unsigned())
    {
        fail("Expected nonnegative integer");
    }
    if (value.is_number_integer() && !value.is_number_unsigned() && value.get<int64_t>() < 0)
    {
        fail("Negative integer");
    }
    uint64_t number = value.get<uint64_t>();
    if (number > maximum)
    {
        fail("Integer exceeds destination range");
    }
    return number;
}

static double number(const Json& value)
{
    if (!value.is_number())
    {
        fail("Expected finite number");
    }
    double result = value.get<double>();
    if (!isfinite(result))
    {
        fail("Nonfinite schema number");
    }
    return result;
}

static bool boolean(const Json& value)
{
    if (!value.is_boolean())
    {
        fail("Expected boolean");
    }
    return value.get<bool>();
}

static const Json& array(const Json& object, const char* key)
{
    static const Json empty = Json::array();
    if (!object.contains(key))
    {
        return empty;
    }
    const Json& value = object.at(key);
    if (!value.is_array())
    {
        fail("Expected array");
    }
    if (value.size() > UINT32_MAX)
    {
        fail("Too many indexed objects");
    }
    return value;
}

static void object(const Json& value)
{
    if (!value.is_object())
    {
        fail("Expected object");
    }
}

static bl_AssetOptionalIndex optionalIndex(const Json& value, const char* key)
{
    if (!value.contains(key))
    {
        return {};
    }
    return {true, (uint32_t)integer(value.at(key), UINT32_MAX)};
}

static void numbers(const Json& value, double* out, size_t count)
{
    if (!value.is_array() || value.size() != count)
    {
        fail("Wrong numeric tuple length");
    }
    for (size_t i = 0; i < count; ++i)
    {
        out[i] = number(value[i]);
    }
}

static uint64_t extensions(const Json& value, bool node = false, bool specGloss = false)
{
    if (!value.contains("extensions"))
    {
        return 0;
    }
    const Json& list = value.at("extensions");
    object(list);
    uint64_t unsupported = 0;
    for (auto entry = list.begin(); entry != list.end(); ++entry)
    {
        if ((!node || entry.key() != "EXT_mesh_gpu_instancing") &&
            (!specGloss || entry.key() != "KHR_materials_pbrSpecularGlossiness"))
        {
            unsupported |= BL_ASSET_UNSUPPORTED_EXTENSION;
        }
    }
    return unsupported;
}

static uint32_t little32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static bl_AssetAccessorShape shape(const Json& value)
{
    static const char* names[7] = {"SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"};
    if (!value.is_string())
    {
        fail("Invalid accessor shape");
    }
    for (unsigned i = 0; i < 7; ++i)
    {
        if (value == names[i])
        {
            return (bl_AssetAccessorShape)i;
        }
    }
    fail("Unknown accessor shape");
    return BL_ASSET_SCALAR;
}

static void descriptors(J_Result* storage, const Json& json, bl_Bytes binary,
    bl_ParsedGltf* out, bool specGloss = false)
{
    object(json);
    object(json.at("asset"));
    if (json.at("asset").at("version") != "2.0")
    {
        fail("Only glTF2.0 is supported", 0, BL_UNSUPPORTED);
    }
    out->gltfVersion = 2;
    out->unsupportedFeatures = extensions(json);
    const Json& buffers = array(json, "buffers");
    bl_AssetBuffer* bufferData =
        (bl_AssetBuffer*)allocate(storage, buffers.size(), sizeof(bl_AssetBuffer));
    out->buffers = bufferData;
    out->bufferCount = buffers.size();
    uint8_t* ownedBinary = (uint8_t*)allocate(storage, binary.count, 1);
    if (binary.count)
    {
        memcpy(ownedBinary, binary.data, binary.count);
    }
    for (size_t i = 0; i < buffers.size(); ++i)
    {
        const Json& item = buffers[i];
        object(item);
        bufferData[i].declaredByteLength = (size_t)integer(item.at("byteLength"));
        if (item.contains("uri"))
        {
            bufferData[i].uri = string(storage, item.at("uri"));
            out->unsupportedFeatures |= BL_ASSET_UNSUPPORTED_BUFFER_URI;
        }
        else if (i == 0)
        {
            bufferData[i].bytes = {ownedBinary, binary.count};
        }
        else
        {
            out->unsupportedFeatures |= BL_ASSET_UNSUPPORTED_BUFFER_URI;
        }
        out->unsupportedFeatures |= extensions(item);
    }
    const Json& views = array(json, "bufferViews");
    bl_AssetBufferView* viewData =
        (bl_AssetBufferView*)allocate(storage, views.size(), sizeof(bl_AssetBufferView));
    out->bufferViews = viewData;
    out->bufferViewCount = views.size();
    for (size_t i = 0; i < views.size(); ++i)
    {
        const Json& item = views[i];
        object(item);
        viewData[i].buffer = (uint32_t)integer(item.at("buffer"), UINT32_MAX);
        viewData[i].byteLength = (size_t)integer(item.at("byteLength"));
        if (item.contains("byteOffset"))
        {
            viewData[i].byteOffset = (size_t)integer(item.at("byteOffset"));
        }
        if (item.contains("byteStride"))
        {
            viewData[i].byteStride = {true, (size_t)integer(item.at("byteStride"))};
        }
        out->unsupportedFeatures |= extensions(item);
    }
    const Json& accessors = array(json, "accessors");
    bl_AssetAccessor* accessorData =
        (bl_AssetAccessor*)allocate(storage, accessors.size(), sizeof(bl_AssetAccessor));
    out->accessors = accessorData;
    out->accessorCount = accessors.size();
    for (size_t i = 0; i < accessors.size(); ++i)
    {
        const Json& item = accessors[i];
        object(item);
        bl_AssetAccessor* target = accessorData + i;
        target->bufferView = optionalIndex(item, "bufferView");
        target->count = (size_t)integer(item.at("count"));
        uint32_t type = (uint32_t)integer(item.at("componentType"), UINT32_MAX);
        if (type != 5120 && type != 5121 && type != 5122 && type != 5123 && type != 5125 &&
            type != 5126)
        {
            fail("Unknown accessor component type", 168);
        }
        target->componentType = (bl_AssetComponentType)type;
        target->shape = shape(item.at("type"));
        if (item.contains("byteOffset"))
        {
            target->byteOffset = (size_t)integer(item.at("byteOffset"));
        }
        if (item.contains("normalized"))
        {
            target->normalized = boolean(item.at("normalized"));
        }
        target->sparsePresent = item.contains("sparse");
        out->unsupportedFeatures |= extensions(item);
    }
    const Json& meshes = array(json, "meshes");
    bl_AssetMesh* meshData =
        (bl_AssetMesh*)allocate(storage, meshes.size(), sizeof(bl_AssetMesh));
    out->meshes = meshData;
    out->meshCount = meshes.size();
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        const Json& item = meshes[i];
        object(item);
        meshData[i].hasName = item.contains("name");
        if (meshData[i].hasName)
        {
            meshData[i].name = string(storage, item.at("name"));
        }
        const Json& primitives = array(item, "primitives");
        bl_AssetPrimitive* primitiveData =
            (bl_AssetPrimitive*)allocate(storage, primitives.size(), sizeof(bl_AssetPrimitive));
        meshData[i].primitives = primitiveData;
        meshData[i].primitiveCount = primitives.size();
        for (size_t j = 0; j < primitives.size(); ++j)
        {
            const Json& primitive = primitives[j];
            object(primitive);
            const Json& attributes = primitive.at("attributes");
            object(attributes);
            bl_AssetPrimitive* target = primitiveData + j;
            bl_AssetAttribute* attributeData =
                (bl_AssetAttribute*)allocate(storage, attributes.size(), sizeof(bl_AssetAttribute));
            target->attributes = attributeData;
            target->attributeCount = attributes.size();
            size_t index = 0;
            for (auto entry = attributes.begin(); entry != attributes.end(); ++entry)
            {
                attributeData[index].semantic = copyString(storage, entry.key());
                attributeData[index].accessor = (uint32_t)integer(entry.value(), UINT32_MAX);
                ++index;
            }
            target->indices = optionalIndex(primitive, "indices");
            target->material = optionalIndex(primitive, "material");
            target->hasMode = primitive.contains("mode");
            if (target->hasMode)
            {
                target->mode = (bl_AssetPrimitiveMode)integer(primitive.at("mode"), 6);
            }
            target->morphTargetCount = array(primitive, "targets").size();
            target->unsupportedFeatures = extensions(primitive);
        }
        out->unsupportedFeatures |= extensions(item);
        if (item.contains("weights"))
        {
            out->unsupportedFeatures |= BL_ASSET_UNSUPPORTED_EXTRA_RENDER_FEATURE;
        }
    }
    const Json& nodes = array(json, "nodes");
    bl_AssetNode* nodeData =
        (bl_AssetNode*)allocate(storage, nodes.size(), sizeof(bl_AssetNode));
    out->nodes = nodeData;
    out->nodeCount = nodes.size();
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const Json& item = nodes[i];
        object(item);
        bl_AssetNode* target = nodeData + i;
        target->hasName = item.contains("name");
        if (target->hasName)
        {
            target->name = string(storage, item.at("name"));
        }
        target->mesh = optionalIndex(item, "mesh");
        target->camera = optionalIndex(item, "camera");
        target->skin = optionalIndex(item, "skin");
        const Json& children = array(item, "children");
        uint32_t* childData = (uint32_t*)allocate(storage, children.size(), sizeof(uint32_t));
        target->children = childData;
        target->childCount = children.size();
        for (size_t j = 0; j < children.size(); ++j)
        {
            childData[j] = (uint32_t)integer(children[j], UINT32_MAX);
        }
        target->hasMatrix = item.contains("matrix");
        if (target->hasMatrix)
        {
            numbers(item.at("matrix"), target->matrix.values, 16);
        }
        target->hasTranslation = item.contains("translation");
        if (target->hasTranslation)
        {
            double tuple[3];
            numbers(item.at("translation"), tuple, 3);
            target->translation = {tuple[0], tuple[1], tuple[2]};
        }
        target->hasRotation = item.contains("rotation");
        if (target->hasRotation)
        {
            double tuple[4];
            numbers(item.at("rotation"), tuple, 4);
            target->rotation = {tuple[0], tuple[1], tuple[2], tuple[3]};
        }
        target->hasScale = item.contains("scale");
        if (target->hasScale)
        {
            double tuple[3];
            numbers(item.at("scale"), tuple, 3);
            target->scale = {tuple[0], tuple[1], tuple[2]};
        }
        target->unsupportedFeatures = extensions(item, true);
        if (item.contains("extensions") &&
            item.at("extensions").contains("EXT_mesh_gpu_instancing"))
        {
            target->hasInstancing = true;
            const Json& attributes =
                item.at("extensions").at("EXT_mesh_gpu_instancing").at("attributes");
            object(attributes);
            target->instanceTranslation = optionalIndex(attributes, "TRANSLATION");
            target->instanceRotation = optionalIndex(attributes, "ROTATION");
            target->instanceScale = optionalIndex(attributes, "SCALE");
            for (auto entry = attributes.begin(); entry != attributes.end(); ++entry)
            {
                if (entry.key() != "TRANSLATION" && entry.key() != "ROTATION" &&
                    entry.key() != "SCALE")
                {
                    target->unsupportedFeatures |= BL_ASSET_UNSUPPORTED_EXTRA_RENDER_FEATURE;
                }
            }
        }
        if (item.contains("weights"))
        {
            target->unsupportedFeatures |= BL_ASSET_UNSUPPORTED_EXTRA_RENDER_FEATURE;
        }
    }
    const Json& materials = array(json, "materials");
    bl_AssetPbrDefinition* materialData =
        (bl_AssetPbrDefinition*)allocate(storage, materials.size(), sizeof(bl_AssetPbrDefinition));
    out->materials = materialData;
    out->materialCount = materials.size();
    for (size_t i = 0; i < materials.size(); ++i)
    {
        const Json& item = materials[i];
        object(item);
        bl_AssetPbrDefinition* target = materialData + i;
        target->hasName = item.contains("name");
        if (target->hasName)
        {
            target->name = string(storage, item.at("name"));
        }
        if (item.contains("pbrMetallicRoughness"))
        {
            const Json& pbr = item.at("pbrMetallicRoughness");
            object(pbr);
            target->hasBaseColorFactor = pbr.contains("baseColorFactor");
            if (target->hasBaseColorFactor)
            {
                double tuple[4];
                numbers(pbr.at("baseColorFactor"), tuple, 4);
                target->baseColorFactor = {tuple[0], tuple[1], tuple[2], tuple[3]};
            }
            target->hasMetallicFactor = pbr.contains("metallicFactor");
            if (target->hasMetallicFactor)
            {
                target->metallicFactor = number(pbr.at("metallicFactor"));
            }
            target->hasRoughnessFactor = pbr.contains("roughnessFactor");
            if (target->hasRoughnessFactor)
            {
                target->roughnessFactor = number(pbr.at("roughnessFactor"));
            }
            if (pbr.contains("baseColorTexture"))
            {
                target->textureInfoFields |= BL_ASSET_TEXTURE_BASE_COLOR;
            }
            if (pbr.contains("metallicRoughnessTexture"))
            {
                target->textureInfoFields |= BL_ASSET_TEXTURE_METALLIC_ROUGHNESS;
            }
            target->unsupportedFeatures |= extensions(pbr);
        }
        target->hasEmissiveFactor = item.contains("emissiveFactor");
        if (target->hasEmissiveFactor)
        {
            double tuple[3];
            numbers(item.at("emissiveFactor"), tuple, 3);
            target->emissiveFactor = {tuple[0], tuple[1], tuple[2]};
        }
        target->hasAlphaMode = item.contains("alphaMode");
        if (target->hasAlphaMode)
        {
            const Json& mode = item.at("alphaMode");
            if (mode == "OPAQUE")
            {
                target->alphaMode = BL_ASSET_ALPHA_OPAQUE;
            }
            else if (mode == "MASK")
            {
                target->alphaMode = BL_ASSET_ALPHA_MASK;
            }
            else if (mode == "BLEND")
            {
                target->alphaMode = BL_ASSET_ALPHA_BLEND;
            }
            else
            {
                fail("Unknown alpha mode");
            }
        }
        target->hasAlphaCutoff = item.contains("alphaCutoff");
        if (target->hasAlphaCutoff)
        {
            target->alphaCutoff = number(item.at("alphaCutoff"));
        }
        target->hasDoubleSided = item.contains("doubleSided");
        if (target->hasDoubleSided)
        {
            target->doubleSided = boolean(item.at("doubleSided"));
        }
        if (item.contains("normalTexture"))
        {
            target->textureInfoFields |= BL_ASSET_TEXTURE_NORMAL;
        }
        if (item.contains("occlusionTexture"))
        {
            target->textureInfoFields |= BL_ASSET_TEXTURE_OCCLUSION;
        }
        if (item.contains("emissiveTexture"))
        {
            target->textureInfoFields |= BL_ASSET_TEXTURE_EMISSIVE;
        }
        target->unsupportedFeatures |= extensions(item, false, specGloss);
        if (specGloss && item.contains("extensions") &&
            item.at("extensions").contains("KHR_materials_pbrSpecularGlossiness"))
        {
            const Json& extension =
                item.at("extensions").at("KHR_materials_pbrSpecularGlossiness");
            object(extension);
            target->unsupportedFeatures |= extensions(extension);
        }
    }
    const Json& scenes = array(json, "scenes");
    bl_AssetScene* sceneData =
        (bl_AssetScene*)allocate(storage, scenes.size(), sizeof(bl_AssetScene));
    out->scenes = sceneData;
    out->sceneCount = scenes.size();
    for (size_t i = 0; i < scenes.size(); ++i)
    {
        object(scenes[i]);
        const Json& roots = array(scenes[i], "nodes");
        uint32_t* rootData = (uint32_t*)allocate(storage, roots.size(), sizeof(uint32_t));
        sceneData[i].roots = rootData;
        sceneData[i].rootCount = roots.size();
        for (size_t j = 0; j < roots.size(); ++j)
        {
            rootData[j] = (uint32_t)integer(roots[j], UINT32_MAX);
        }
        out->unsupportedFeatures |= extensions(scenes[i]);
    }
    out->defaultScene = optionalIndex(json, "scene");
    const Json& used = array(json, "extensionsUsed");
    bl_String* usedData = (bl_String*)allocate(storage, used.size(), sizeof(bl_String));
    out->extensionsUsed = usedData;
    out->extensionsUsedCount = used.size();
    for (size_t i = 0; i < used.size(); ++i)
    {
        usedData[i] = string(storage, used[i]);
    }
    const Json& required = array(json, "extensionsRequired");
    bl_String* requiredData = (bl_String*)allocate(storage, required.size(), sizeof(bl_String));
    out->extensionsRequired = requiredData;
    out->extensionsRequiredCount = required.size();
    for (size_t i = 0; i < required.size(); ++i)
    {
        requiredData[i] = string(storage, required[i]);
    }
    out->animationCount = array(json, "animations").size();
    out->skinCount = array(json, "skins").size();
    out->imageCount = array(json, "images").size();
    out->textureCount = array(json, "textures").size();
    out->samplerCount = array(json, "samplers").size();
    out->cameraCount = array(json, "cameras").size();
    for (const Json& camera : array(json, "cameras"))
    {
        object(camera);
        const Json& type = camera.at("type");
        if (type == "perspective")
        {
            const Json& projection = camera.at("perspective");
            object(projection);
            double nearPlane = number(projection.at("znear"));
            double fov = number(projection.at("yfov"));
            if (nearPlane <= 0 || fov <= 0 ||
                (projection.contains("zfar") && number(projection.at("zfar")) <= nearPlane) ||
                (projection.contains("aspectRatio") && number(projection.at("aspectRatio")) <= 0))
            {
                fail("Invalid ignored perspective camera definition");
            }
        }
        else if (type == "orthographic")
        {
            const Json& projection = camera.at("orthographic");
            object(projection);
            double xmag = number(projection.at("xmag"));
            double ymag = number(projection.at("ymag"));
            double nearPlane = number(projection.at("znear"));
            double farPlane = number(projection.at("zfar"));
            if (xmag == 0 || ymag == 0 || nearPlane < 0 || farPlane <= nearPlane)
            {
                fail("Invalid ignored orthographic camera definition");
            }
        }
        else
        {
            fail("Unknown ignored camera type");
        }
    }
}

static bl_AssetTextureInfo textureInfo(const Json& object, const char* key, bool modifiers = true)
{
    bl_AssetTextureInfo result = {};
    if (!object.contains(key))
    {
        return result;
    }
    const Json& value = object.at(key);
    if (!value.is_object())
    {
        fail("Texture info must be an object");
    }
    result.present = true;
    result.index = (uint32_t)integer(value.at("index"), UINT32_MAX);
    result.texCoord = optionalIndex(value, "texCoord");
    result.hasScale = modifiers && value.contains("scale");
    if (result.hasScale)
    {
        result.scale = number(value.at("scale"));
    }
    result.hasStrength = modifiers && value.contains("strength");
    if (result.hasStrength)
    {
        result.strength = number(value.at("strength"));
    }
    result.unsupportedFeatures = extensions(value);
    return result;
}

static void textureDescriptors(J_Result* storage, const Json& json, bl_ParsedGltf3* out)
{
    const Json& images = array(json, "images");
    bl_AssetImage* imageData =
        (bl_AssetImage*)allocate(storage, images.size(), sizeof(bl_AssetImage));
    out->images = imageData;
    for (size_t i = 0; i < images.size(); ++i)
    {
        const Json& item = images[i];
        object(item);
        imageData[i].bufferView = optionalIndex(item, "bufferView");
        imageData[i].hasMimeType = item.contains("mimeType");
        if (imageData[i].hasMimeType)
        {
            imageData[i].mimeType = string(storage, item.at("mimeType"));
        }
        imageData[i].hasUri = item.contains("uri");
        if (imageData[i].hasUri)
        {
            imageData[i].uri = string(storage, item.at("uri"));
        }
        imageData[i].unsupportedFeatures = extensions(item);
    }
    const Json& textures = array(json, "textures");
    bl_AssetTexture* textureData =
        (bl_AssetTexture*)allocate(storage, textures.size(), sizeof(bl_AssetTexture));
    out->textures = textureData;
    for (size_t i = 0; i < textures.size(); ++i)
    {
        object(textures[i]);
        textureData[i].source = optionalIndex(textures[i], "source");
        textureData[i].sampler = optionalIndex(textures[i], "sampler");
        textureData[i].unsupportedFeatures = extensions(textures[i]);
    }
    const Json& samplers = array(json, "samplers");
    bl_AssetSampler* samplerData =
        (bl_AssetSampler*)allocate(storage, samplers.size(), sizeof(bl_AssetSampler));
    out->samplers = samplerData;
    for (size_t i = 0; i < samplers.size(); ++i)
    {
        object(samplers[i]);
        samplerData[i].magFilter = optionalIndex(samplers[i], "magFilter");
        samplerData[i].minFilter = optionalIndex(samplers[i], "minFilter");
        samplerData[i].wrapS = optionalIndex(samplers[i], "wrapS");
        samplerData[i].wrapT = optionalIndex(samplers[i], "wrapT");
        out->document.unsupportedFeatures |= extensions(samplers[i]);
    }
    const Json& materials = array(json, "materials");
    bl_AssetMaterialInputs* inputs =
        (bl_AssetMaterialInputs*)allocate(storage, materials.size(), sizeof(bl_AssetMaterialInputs));
    out->materialInputs = inputs;
    for (size_t i = 0; i < materials.size(); ++i)
    {
        const Json& material = materials[i];
        if (material.contains("pbrMetallicRoughness"))
        {
            const Json& pbr = material.at("pbrMetallicRoughness");
            inputs[i].baseColor = textureInfo(pbr, "baseColorTexture");
            inputs[i].metallicRoughness = textureInfo(pbr, "metallicRoughnessTexture");
        }
        inputs[i].normal = textureInfo(material, "normalTexture");
        inputs[i].occlusion = textureInfo(material, "occlusionTexture");
        inputs[i].emissive = textureInfo(material, "emissiveTexture");
    }
}

static void specularGlossinessDescriptors(J_Result* storage, const Json& json,
    bl_ParsedGltf5* out)
{
    const Json& materials = array(json, "materials");
    bl_AssetSpecularGlossinessInputs* inputs = (bl_AssetSpecularGlossinessInputs*)allocate(
        storage, materials.size(), sizeof(bl_AssetSpecularGlossinessInputs));
    out->specularGlossinessInputs = inputs;
    for (size_t i = 0; i < materials.size(); ++i)
    {
        const Json& material = materials[i];
        if (!material.contains("extensions") ||
            !material.at("extensions").contains("KHR_materials_pbrSpecularGlossiness"))
        {
            continue;
        }
        const Json& extension =
            material.at("extensions").at("KHR_materials_pbrSpecularGlossiness");
        object(extension);
        bl_AssetSpecularGlossinessInputs* target = inputs + i;
        target->present = true;
        target->hasDiffuseFactor = extension.contains("diffuseFactor");
        if (target->hasDiffuseFactor)
        {
            double tuple[4];
            numbers(extension.at("diffuseFactor"), tuple, 4);
            target->diffuseFactor = {tuple[0], tuple[1], tuple[2], tuple[3]};
        }
        target->hasSpecularFactor = extension.contains("specularFactor");
        if (target->hasSpecularFactor)
        {
            double tuple[3];
            numbers(extension.at("specularFactor"), tuple, 3);
            target->specularFactor = {tuple[0], tuple[1], tuple[2]};
        }
        target->hasGlossinessFactor = extension.contains("glossinessFactor");
        if (target->hasGlossinessFactor)
        {
            target->glossinessFactor = number(extension.at("glossinessFactor"));
        }
        target->diffuse = textureInfo(extension, "diffuseTexture", false);
        target->specularGlossiness =
            textureInfo(extension, "specularGlossinessTexture", false);
    }
}

static bl_AssetOptionalSize optionalSize(const Json& value, const char* key)
{
    if (!value.contains(key))
    {
        return {};
    }
    return {true, (size_t)integer(value.at(key))};
}

static bl_NumberSpan bounds(J_Result* storage, const Json& item, const char* key)
{
    const Json& values = array(item, key);
    double* data = (double*)allocate(storage, values.size(), sizeof(double));
    for (size_t i = 0; i < values.size(); ++i)
    {
        data[i] = number(values[i]);
    }
    return {data, values.size()};
}

static void accessorDescriptors(J_Result* storage, const Json& json, bl_ParsedGltf4* out)
{
    bl_ParsedGltf* document = &out->document3.document;
    const Json& accessors = array(json, "accessors");
    bl_AssetAccessorInputs* inputs =
        (bl_AssetAccessorInputs*)allocate(storage, accessors.size(), sizeof(bl_AssetAccessorInputs));
    out->accessorInputs = inputs;
    for (size_t i = 0; i < accessors.size(); ++i)
    {
        const Json& item = accessors[i];
        inputs[i].hasByteOffset = item.contains("byteOffset");
        inputs[i].hasNormalized = item.contains("normalized");
        inputs[i].hasMin = item.contains("min");
        inputs[i].hasMax = item.contains("max");
        if (inputs[i].hasMin)
        {
            inputs[i].min = bounds(storage, item, "min");
        }
        if (inputs[i].hasMax)
        {
            inputs[i].max = bounds(storage, item, "max");
        }
        if (document->accessors[i].sparsePresent)
        {
            const Json& sparse = item.at("sparse");
            object(sparse);
            const Json& indices = sparse.at("indices");
            const Json& values = sparse.at("values");
            object(indices);
            object(values);
            bl_AssetSparseAccessor* target = &inputs[i].sparse;
            target->count = (size_t)integer(sparse.at("count"));
            if (!target->count)
            {
                fail("Sparse count must be positive");
            }
            target->indicesBufferView = (uint32_t)integer(indices.at("bufferView"), UINT32_MAX);
            target->indicesByteOffset = optionalSize(indices, "byteOffset");
            target->indicesComponentType =
                (bl_AssetComponentType)integer(indices.at("componentType"), UINT32_MAX);
            target->valuesBufferView = (uint32_t)integer(values.at("bufferView"), UINT32_MAX);
            target->valuesByteOffset = optionalSize(values, "byteOffset");
            document->unsupportedFeatures |=
                extensions(sparse) | extensions(indices) | extensions(values);
        }
    }
    const Json& views = array(json, "bufferViews");
    bl_AssetBufferViewInputs* viewInputs = (bl_AssetBufferViewInputs*)allocate(
        storage, views.size(), sizeof(bl_AssetBufferViewInputs));
    out->bufferViewInputs = viewInputs;
    for (size_t i = 0; i < views.size(); ++i)
    {
        viewInputs[i].hasByteOffset = views[i].contains("byteOffset");
        viewInputs[i].target = optionalIndex(views[i], "target");
    }
}

static bl_Status diagnostic(J_Result* storage, bl_AssetDiagnostic* target,
    const char* message, uint32_t code, bl_Status status)
{
    target->sourceErrorCode = code;
    if (storage)
    {
        try
        {
            target->message = copyString(storage, message);
        }
        catch (...)
        {
            target->message = {};
        }
    }
    return status;
}

static bl_Status parseGltfCommon(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out,
    bl_ParsedGltf3* extended, bl_ParsedGltf4* full = NULL, bl_ParsedGltf5* materials = NULL)
{
    (void)base;
    if (!user || !out || !bytes.data || !bytes.count)
    {
        return BL_INVALID_ARGUMENT;
    }
    J_Result* storage = NULL;
    try
    {
        storage = createResult((bl_AssetDecoder*)user);
        out->allocation = storage;
        if (bytes.count < 4 || little32(bytes.data) != UINT32_C(0x46546c67))
        {
            (void)Json::parse(bytes.data, bytes.data + bytes.count);
            fail("Initial provider accepts self-contained GLB only", 0, BL_UNSUPPORTED);
        }
        if (bytes.count < 20 || little32(bytes.data + 4) != 2 ||
            little32(bytes.data + 8) != bytes.count)
        {
            fail("Invalid GLB2 header");
        }
        size_t jsonBytes = little32(bytes.data + 12);
        if (little32(bytes.data + 16) != UINT32_C(0x4e4f534a))
        {
            fail("GLB first chunk is not JSON", 164);
        }
        if ((jsonBytes & 3) || jsonBytes > bytes.count - 20)
        {
            fail("Invalid GLB JSON range");
        }
        size_t binHeader = 20 + jsonBytes;
        bool hasBin = binHeader != bytes.count;
        bl_Bytes binary = {};
        if (hasBin)
        {
            if (bytes.count - binHeader < 8 ||
                little32(bytes.data + binHeader + 4) != UINT32_C(0x004e4942))
            {
                fail("GLB BIN chunk missing", 165);
            }
            size_t binBytes = little32(bytes.data + binHeader);
            if ((binBytes & 3) || binBytes != bytes.count - binHeader - 8)
            {
                fail("Invalid GLB BIN range");
            }
            binary = {bytes.data + binHeader + 8, binBytes};
        }
        Json json = Json::parse(bytes.data + 20, bytes.data + 20 + jsonBytes);
        if (!hasBin && !array(json, "buffers").empty())
        {
            fail("GLB BIN chunk missing", 165);
        }
        descriptors(storage, json, binary, out, materials != NULL);
        if (extended)
        {
            textureDescriptors(storage, json, extended);
        }
        if (full)
        {
            accessorDescriptors(storage, json, full);
        }
        if (materials)
        {
            specularGlossinessDescriptors(storage, json, materials);
        }
        return BL_OK;
    }
    catch (const J_Failure& error)
    {
        return diagnostic(storage, &out->diagnostic, error.message, error.code, error.status);
    }
    catch (const std::bad_alloc&)
    {
        return BL_OUT_OF_MEMORY;
    }
    catch (const Json::exception& error)
    {
        return diagnostic(storage, &out->diagnostic, error.what(), 0, BL_INVALID_ARGUMENT);
    }
    catch (...)
    {
        return diagnostic(storage, &out->diagnostic, "Unexpected parser failure", 0, BL_HOST_ERROR);
    }
}

static bl_Status parseGltf(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out)
{
    if (!out || out->structSize != sizeof(*out) || out->version != BL_ASSET_DOCUMENT_VERSION)
    {
        return BL_INVALID_ARGUMENT;
    }
    return parseGltfCommon(user, bytes, base, out, NULL);
}

static bl_Status parseGltf3(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out)
{
    if (!out || out->structSize != sizeof(bl_ParsedGltf3) ||
        out->version != BL_ASSET_DOCUMENT_VERSION_3 ||
        (uintptr_t)out % alignof(bl_ParsedGltf3))
    {
        return BL_INVALID_ARGUMENT;
    }
    return parseGltfCommon(user, bytes, base, out, (bl_ParsedGltf3*)out);
}

static bl_Status parseGltf4(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out)
{
    if (!out || out->structSize != sizeof(bl_ParsedGltf4) ||
        out->version != BL_ASSET_DOCUMENT_VERSION_4 ||
        (uintptr_t)out % alignof(bl_ParsedGltf4))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_ParsedGltf4* full = (bl_ParsedGltf4*)out;
    return parseGltfCommon(user, bytes, base, out, &full->document3, full);
}

static bl_Status parseGltf5(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out)
{
    if (!out || out->structSize != sizeof(bl_ParsedGltf5) ||
        out->version != BL_ASSET_DOCUMENT_VERSION_5 ||
        (uintptr_t)out % alignof(bl_ParsedGltf5))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_ParsedGltf5* full = (bl_ParsedGltf5*)out;
    return parseGltfCommon(user, bytes, base, out, &full->document4.document3,
        &full->document4, full);
}

static bl_Status parseEnvironment(void* user, bl_Bytes bytes, bl_ParsedEnvironment* out)
{
    if (!user || !out || out->structSize != sizeof(*out) ||
        out->version != BL_ASSET_DOCUMENT_VERSION || !bytes.data || bytes.count < 9)
    {
        return BL_INVALID_ARGUMENT;
    }
    J_Result* storage = NULL;
    try
    {
        storage = createResult((bl_AssetDecoder*)user);
        out->allocation = storage;
        const uint8_t magic[8] = {0x86, 0x16, 0x87, 0x96, 0xf6, 0xd6, 0x96, 0x36};
        if (memcmp(bytes.data, magic, 8))
        {
            fail("Invalid .env magic");
        }
        size_t end = 8;
        while (end < bytes.count && bytes.data[end])
        {
            ++end;
        }
        if (end == bytes.count)
        {
            fail("Unterminated .env manifest");
        }
        Json json = Json::parse(bytes.data + 8, bytes.data + end);
        object(json);
        out->hasFileVersion = json.contains("version");
        if (out->hasFileVersion)
        {
            out->fileVersion = (uint32_t)integer(json.at("version"), UINT32_MAX);
        }
        out->width = (uint32_t)integer(json.at("width"), UINT32_MAX);
        const Json& irradiance = json.at("irradiance");
        object(irradiance);
        const char* keys[9] = {"x", "y", "z", "xx", "yy", "zz", "yz", "zx", "xy"};
        for (unsigned i = 0; i < 9; ++i)
        {
            numbers(irradiance.at(keys[i]), out->irradiancePolynomial + i * 3, 3);
        }
        const Json& entries = array(json.at("specular"), "mipmaps");
        bl_EncodedEnvironmentFace* faces =
            (bl_EncodedEnvironmentFace*)allocate(storage, entries.size(),
                sizeof(bl_EncodedEnvironmentFace));
        out->faces = faces;
        out->faceCount = entries.size();
        bl_String mime = json.contains("imageType") ? string(storage, json.at("imageType"))
                                                    : copyString(storage, "image/png");
        size_t binaryStart = end + 1;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            object(entries[i]);
            size_t position = (size_t)integer(entries[i].at("position"));
            size_t length = (size_t)integer(entries[i].at("length"));
            if (position > bytes.count - binaryStart ||
                length > bytes.count - binaryStart - position)
            {
                fail("Environment encoded face outside input");
            }
            uint8_t* encoded = (uint8_t*)allocate(storage, length, 1);
            memcpy(encoded, bytes.data + binaryStart + position, length);
            faces[i].mip = (uint32_t)(i / 6);
            faces[i].face = (bl_EnvironmentFace)(i % 6);
            faces[i].mimeType = mime;
            faces[i].encoded = {encoded, length};
        }
        return BL_OK;
    }
    catch (const J_Failure& error)
    {
        return diagnostic(storage, &out->diagnostic, error.message, error.code, error.status);
    }
    catch (const std::bad_alloc&)
    {
        return BL_OUT_OF_MEMORY;
    }
    catch (const Json::exception& error)
    {
        return diagnostic(storage, &out->diagnostic, error.what(), 0, BL_INVALID_ARGUMENT);
    }
    catch (...)
    {
        return diagnostic(storage, &out->diagnostic, "Unexpected environment parser failure",
            0, BL_HOST_ERROR);
    }
}

static void releaseGltf(void*, bl_ParsedGltf* result)
{
    if (result)
    {
        freeResult(result->allocation);
        *result = {};
    }
}

static void releaseGltf3(void*, bl_ParsedGltf* result)
{
    if (result)
    {
        freeResult(result->allocation);
        *result = {};
    }
}

static void releaseEnvironment(void*, bl_ParsedEnvironment* result)
{
    if (result)
    {
        freeResult(result->allocation);
        *result = {};
    }
}

bl_Status bl_createAssetDecoder(const bl_Allocator* allocator, bl_AssetDecoder** out,
    bl_AssetService* service)
{
    if (!allocator || !out || !service || !allocator->allocate || !allocator->deallocate)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_AssetDecoder* decoder =
        (bl_AssetDecoder*)allocator->allocate(allocator->userData, sizeof(bl_AssetDecoder), 16);
    if (!decoder)
    {
        return BL_OUT_OF_MEMORY;
    }
    if ((uintptr_t)decoder & 15)
    {
        allocator->deallocate(allocator->userData, decoder, sizeof(*decoder), 16);
        return BL_INVALID_ARGUMENT;
    }
    decoder->allocator = *allocator;
    *service = {sizeof(bl_AssetService), BL_ASSET_SERVICE_VERSION, decoder, parseGltf,
        releaseGltf, parseEnvironment, releaseEnvironment};
    *out = decoder;
    return BL_OK;
}

void bl_destroyAssetDecoder(bl_AssetDecoder* decoder)
{
    if (decoder)
    {
        bl_Allocator allocator = decoder->allocator;
        allocator.deallocate(allocator.userData, decoder, sizeof(*decoder), 16);
    }
}

bl_Status bl_assetDecoderService3(bl_AssetDecoder* decoder, bl_AssetService* service)
{
    if (!decoder || !service)
    {
        return BL_INVALID_ARGUMENT;
    }
    *service = {sizeof(bl_AssetService), BL_ASSET_SERVICE_VERSION_3, decoder, parseGltf3,
        releaseGltf3, parseEnvironment, releaseEnvironment};
    return BL_OK;
}

bl_Status bl_assetDecoderService4(bl_AssetDecoder* decoder, bl_AssetService* service)
{
    if (!decoder || !service)
    {
        return BL_INVALID_ARGUMENT;
    }
    *service = {sizeof(bl_AssetService), BL_ASSET_SERVICE_VERSION_4, decoder, parseGltf4,
        releaseGltf, parseEnvironment, releaseEnvironment};
    return BL_OK;
}

bl_Status bl_assetDecoderService5(bl_AssetDecoder* decoder, bl_AssetService* service)
{
    if (!decoder || !service)
    {
        return BL_INVALID_ARGUMENT;
    }
    *service = {sizeof(bl_AssetService), BL_ASSET_SERVICE_VERSION_5, decoder, parseGltf5,
        releaseGltf, parseEnvironment, releaseEnvironment};
    return BL_OK;
}
