// Dependency bytes and the embedded MIT notice are verified by LiteCgltf.cmake.
#define CGLTF_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <cgltf.h>
#undef CGLTF_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "GltfParserPrivate.h"
#include <stdbool.h>

static bool motionIndex(const jsmntok_t* token, const char* json)
{
    if (token->type != JSMN_PRIMITIVE || token->start == token->end)
    {
        return false;
    }
    uint32_t value = 0;
    for (ptrdiff_t i = token->start; i < token->end; ++i)
    {
        if (json[i] < '0' || json[i] > '9')
        {
            return false;
        }
        uint32_t digit = (uint32_t)(json[i] - '0');
        if (value > ((uint32_t)INT32_MAX - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
    }
    return true;
}

static cgltf_result motionTarget(const jsmntok_t* tokens, const uint8_t* json, int target)
{
    int field = target + 1;
    for (int i = 0; i < tokens[target].size; ++i)
    {
        int value = field + 1;
        if (cgltf_json_strcmp(tokens + field, json, "node") == 0 &&
            !motionIndex(tokens + value, (const char*)json))
        {
            return cgltf_result_invalid_gltf;
        }
        if (cgltf_json_strcmp(tokens + field, json, "extensions") == 0 && tokens[value].size != 0)
        {
            return cgltf_result_unknown_format;
        }
        field = cgltf_skip_json(tokens, value);
    }
    return cgltf_result_success;
}

static cgltf_result motionArrayPresence(const jsmntok_t* tokens, const uint8_t* json, int array,
                                        bool samplers)
{
    int item = array + 1;
    for (int i = 0; i < tokens[array].size; ++i)
    {
        int field = item + 1;
        bool first = false;
        bool second = false;
        for (int j = 0; j < tokens[item].size; ++j)
        {
            int value = field + 1;
            bool input =
                cgltf_json_strcmp(tokens + field, json, samplers ? "input" : "sampler") == 0;
            bool output =
                cgltf_json_strcmp(tokens + field, json, samplers ? "output" : "target") == 0;
            first |= input;
            second |= output;
            if ((input || (samplers && output)) && !motionIndex(tokens + value, (const char*)json))
            {
                return cgltf_result_invalid_gltf;
            }
            if (!samplers && output)
            {
                cgltf_result result = motionTarget(tokens, json, value);
                if (result != cgltf_result_success)
                {
                    return result;
                }
            }
            if (samplers && cgltf_json_strcmp(tokens + field, json, "interpolation") == 0)
            {
                if (tokens[value].type != JSMN_STRING)
                {
                    return cgltf_result_invalid_gltf;
                }
                if (cgltf_json_strcmp(tokens + value, json, "LINEAR") &&
                    cgltf_json_strcmp(tokens + value, json, "STEP") &&
                    cgltf_json_strcmp(tokens + value, json, "CUBICSPLINE"))
                {
                    return cgltf_result_unknown_format;
                }
            }
            field = cgltf_skip_json(tokens, value);
        }
        if (!first || !second)
        {
            return cgltf_result_invalid_gltf;
        }
        item = cgltf_skip_json(tokens, item);
    }
    return cgltf_result_success;
}

static cgltf_result motionPresence(const jsmntok_t* tokens, const uint8_t* json, int array,
                                   bool skins)
{
    int item = array + 1;
    for (int i = 0; i < tokens[array].size; ++i)
    {
        int field = item + 1;
        for (int j = 0; j < tokens[item].size; ++j)
        {
            int value = field + 1;
            if (skins)
            {
                if (cgltf_json_strcmp(tokens + field, json, "inverseBindMatrices") == 0 ||
                    cgltf_json_strcmp(tokens + field, json, "skeleton") == 0)
                {
                    if (!motionIndex(tokens + value, (const char*)json))
                    {
                        return cgltf_result_invalid_gltf;
                    }
                }
                if (cgltf_json_strcmp(tokens + field, json, "joints") == 0)
                {
                    for (int k = 0; k < tokens[value].size; ++k)
                    {
                        if (!motionIndex(tokens + value + 1 + k, (const char*)json))
                        {
                            return cgltf_result_invalid_gltf;
                        }
                    }
                }
            }
            else if (cgltf_json_strcmp(tokens + field, json, "samplers") == 0 ||
                     cgltf_json_strcmp(tokens + field, json, "channels") == 0)
            {
                cgltf_result result = motionArrayPresence(
                    tokens, json, value, cgltf_json_strcmp(tokens + field, json, "samplers") == 0);
                if (result != cgltf_result_success)
                {
                    return result;
                }
            }
            field = cgltf_skip_json(tokens, value);
        }
        item = cgltf_skip_json(tokens, item);
    }
    return cgltf_result_success;
}

cgltf_result l_cgltfParse(const cgltf_options* options, const void* input, size_t bytes,
                          cgltf_data** out, jmp_buf* jump)
{
    // The pinned parser's partial-array OOM cleanup can dereference a null array.
    // This C-only call tree has no C++ destructors. Abort before that cleanup and
    // release the exact allocation transaction, without changing vendor bytes.
    if (setjmp(*jump))
    {
        return cgltf_result_out_of_memory;
    }
    return cgltf_parse(options, input, bytes, out);
}

cgltf_result l_cgltfValidatePresence(const cgltf_options* options, const cgltf_data* data)
{
    // cgltf defaults offset to zero and does not retain its JSON presence.
    // glTF forbids even byteOffset:0 when bufferView is absent.
    const char* json = data->json;
    size_t length = data->json_size;
    jsmn_parser parser = {0};
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, json, length, NULL, 0);
    if (count <= 0)
    {
        return cgltf_result_invalid_gltf;
    }
    if ((size_t)count + 1 > SIZE_MAX / sizeof(jsmntok_t))
    {
        return cgltf_result_invalid_gltf;
    }
    size_t bytes = ((size_t)count + 1) * sizeof(jsmntok_t);
    jsmntok_t* tokens = (jsmntok_t*)options->memory.alloc_func(options->memory.user_data, bytes);
    if (!tokens)
    {
        return cgltf_result_out_of_memory;
    }
    jsmn_init(&parser);
    cgltf_result status = cgltf_result_success;
    if (jsmn_parse(&parser, json, length, tokens, (size_t)count) != count)
    {
        status = cgltf_result_invalid_gltf;
    }
    int cursor = 1;
    for (int i = 0; i < tokens[0].size && status == cgltf_result_success; ++i)
    {
        int value = cursor + 1;
        if (cgltf_json_strcmp(tokens + cursor, (const uint8_t*)json, "accessors") == 0)
        {
            int accessor = value + 1;
            for (int j = 0; j < tokens[value].size && status == cgltf_result_success; ++j)
            {
                bool view = false;
                bool offset = false;
                int member = accessor + 1;
                for (int k = 0; k < tokens[accessor].size; ++k)
                {
                    view |=
                        cgltf_json_strcmp(tokens + member, (const uint8_t*)json, "bufferView") == 0;
                    offset |=
                        cgltf_json_strcmp(tokens + member, (const uint8_t*)json, "byteOffset") == 0;
                    member = cgltf_skip_json(tokens, member + 1);
                }
                if (!view && offset)
                {
                    status = cgltf_result_invalid_gltf;
                }
                accessor = cgltf_skip_json(tokens, accessor);
            }
        }
        if (cgltf_json_strcmp(tokens + cursor, (const uint8_t*)json, "skins") == 0 ||
            cgltf_json_strcmp(tokens + cursor, (const uint8_t*)json, "animations") == 0)
        {
            status = motionPresence(
                tokens, (const uint8_t*)json, value,
                cgltf_json_strcmp(tokens + cursor, (const uint8_t*)json, "skins") == 0);
        }
        if (cgltf_json_strcmp(tokens + cursor, (const uint8_t*)json, "nodes") == 0)
        {
            int node = value + 1;
            for (int j = 0; j < tokens[value].size; ++j)
            {
                int field = node + 1;
                for (int k = 0; k < tokens[node].size; ++k)
                {
                    if (cgltf_json_strcmp(tokens + field, (const uint8_t*)json, "skin") == 0 &&
                        !motionIndex(tokens + field + 1, json))
                    {
                        status = cgltf_result_invalid_gltf;
                    }
                    field = cgltf_skip_json(tokens, field + 1);
                }
                node = cgltf_skip_json(tokens, node);
            }
        }
        cursor = cgltf_skip_json(tokens, value);
    }
    options->memory.free_func(options->memory.user_data, tokens);
    return status;
}
