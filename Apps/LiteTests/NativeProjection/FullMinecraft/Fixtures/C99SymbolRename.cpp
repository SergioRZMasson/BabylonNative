#include <babylon_lite.h>

// Deliberately rejected compile-only reproducer: these are the argument shapes
// the actual bblitec native output emits, renamed onto current C99 functions.
// No placeholder implementation or renderer dependency is supplied.
void ProjectedShaderCalls(bl_Runtime* runtime, bl_ShaderMaterial material)
{
    bl_ShaderMaterial result{};
    bl_createShaderMaterial(runtime, 1u, &result);
    double values[3]{0.7, 0.82, 0.92};
    bl_setShaderUniform(material, 7u, {values, 3});
}
