#include "babylon_lite.h"
#include <assert.h>
#include <malloc.h>

static void* allocate(void*, size_t bytes, size_t alignment)
{
    return _aligned_malloc(bytes, alignment);
}

static void deallocate(void*, void* memory, size_t, size_t)
{
    _aligned_free(memory);
}

int main()
{
    bl_RuntimeOptions options = {};
    options.allocator = {nullptr, allocate, deallocate};
    bl_Runtime* runtime = nullptr;
    assert(bl_createRuntime(&options, &runtime) == BL_OK);
    bl_TransformNode parent = {};
    bl_TransformNode child = {};
    assert(bl_createTransformNode(runtime, {}, nullptr, &parent) == BL_OK);
    assert(bl_createTransformNode(runtime, {}, nullptr, &child) == BL_OK);
    assert(bl_setNodeParent(child, parent) == BL_OK);
    assert(bl_setNodePosition(parent, {2, 3, 4}) == BL_OK);
    bl_Mat4 matrix = {};
    uint64_t version = 0;
    assert(bl_getNodeWorldMatrix(child, &matrix, &version) == BL_OK);
    assert(matrix.values[12] == 2 && matrix.values[13] == 3 && matrix.values[14] == 4);
    bl_GeometryData box = {};
    bl_GeometryData sphere = {};
    assert(bl_createBoxData(runtime, nullptr, &box) == BL_OK);
    assert(bl_createSphereData(runtime, nullptr, &sphere) == BL_OK);
    assert(box.vertexCount == 24 && sphere.vertexCount == 2415);
    const bl_VertexSemantic attribute = BL_ATTRIBUTE_POSITION;
    const bl_ShaderUniformDecl uniform = {{"value", 5}, BL_UNIFORM_F32, {}, false};
    bl_ShaderMaterialOptions materialOptions = {};
    materialOptions.attributes = &attribute;
    materialOptions.attributeCount = 1;
    materialOptions.uniforms = &uniform;
    materialOptions.uniformCount = 1;
    materialOptions.vertexSource = {"vertex-data-only", 16};
    materialOptions.fragmentSource = {"fragment-data-only", 18};
    bl_ShaderMaterial material = {};
    assert(bl_createShaderMaterial(runtime, &materialOptions, &material) == BL_OK);
    assert(bl_setShaderFloat(material, {"value", 5}, 7) == BL_OK);
    bl_ShaderUniformView value = {};
    assert(bl_getShaderUniform(material, {"value", 5}, &value) == BL_OK);
    assert(value.values.data[0] == 7);
    bl_ArcRotateCamera arc{};
    assert(bl_createArcRotateCamera(runtime, 0, 1, 5, {}, &arc) == BL_OK);
    bl_ArcRotateControl controls{};
    assert(bl_attachControl(arc, {}, nullptr, &controls) == BL_OK);
    bl_ArcRotateControlOptions controlOptions{};
    assert(bl_setArcRotateControlOptions(controls, &controlOptions) == BL_OK);
    bl_ArcRotateCameraLimitPatch patch{};
    assert(bl_setArcRotateCameraLimitFields(arc, &patch) == BL_OK);
    bl_HemisphericLight light{};
    assert(bl_createHemisphericLight(runtime, nullptr, &light) == BL_OK);
    bl_StandardMaterial standard{};
    assert(bl_createStandardMaterial(runtime, &standard) == BL_OK);
    assert(bl_disposeRuntime(runtime) == BL_OK);
    return 0;
}
