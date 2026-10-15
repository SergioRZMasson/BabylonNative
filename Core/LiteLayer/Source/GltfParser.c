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
        cursor = cgltf_skip_json(tokens, value);
    }
    options->memory.free_func(options->memory.user_data, tokens);
    return status;
}
