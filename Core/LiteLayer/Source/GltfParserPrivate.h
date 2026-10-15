#ifndef BL_GLTF_PARSER_PRIVATE_H
#define BL_GLTF_PARSER_PRIVATE_H

#include <cgltf.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C"
{
#endif

cgltf_result l_cgltfParse(const cgltf_options* options, const void* input, size_t bytes,
                          cgltf_data** out, jmp_buf* jump);
cgltf_result l_cgltfValidatePresence(const cgltf_options* options, const cgltf_data* data);

#ifdef __cplusplus
}
#endif

#endif
