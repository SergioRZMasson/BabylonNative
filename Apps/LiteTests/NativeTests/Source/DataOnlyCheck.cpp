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
    bl_GeometryData ground = {};
    assert(bl_createFlatGroundData(runtime, nullptr, &ground) == BL_OK);
    assert(ground.vertexCount == 4 && ground.indexCount == 6);
    assert(bl_freeGeometryData(runtime, &ground) == BL_OK);
    bl_GeometryData procedural = {};
    assert(bl_createCylinderData(runtime, nullptr, &procedural) == BL_OK);
    assert(procedural.vertexCount == 102 && procedural.indexCount == 288);
    assert(bl_freeGeometryData(runtime, &procedural) == BL_OK);
    assert(bl_createPlaneData(runtime, nullptr, &procedural) == BL_OK);
    assert(bl_freeGeometryData(runtime, &procedural) == BL_OK);
    assert(bl_createDiscData(runtime, nullptr, &procedural) == BL_OK);
    assert(bl_freeGeometryData(runtime, &procedural) == BL_OK);
    assert(bl_createPolyhedronData(runtime, nullptr, &procedural) == BL_OK);
    assert(bl_freeGeometryData(runtime, &procedural) == BL_OK);
    const bl_Vec3 points[] = {{0, 0, 0}, {1, 0, 0}};
    const bl_Vec3Span row = {points, 2};
    const bl_Vec3Span rows[] = {row, row};
    bl_RibbonOptions ribbon = {};
    ribbon.pathArray = {rows, 2};
    assert(bl_createRibbonData(runtime, &ribbon, &procedural) == BL_OK);
    assert(bl_freeGeometryData(runtime, &procedural) == BL_OK);
    bl_TubeOptions tube = {};
    tube.path = row;
    tube.tessellation = {true, 1};
    assert(bl_createTubeData(runtime, &tube, &procedural) == BL_OK);
    assert(procedural.vertexCount == 4 && procedural.indexCount == 6);
    assert(bl_freeGeometryData(runtime, &procedural) == BL_OK);
    bl_ExtrudeShapeOptions extrude = {};
    extrude.path = row;
    extrude.shape = {};
    assert(bl_createExtrudeShapeData(runtime, &extrude, &procedural) == BL_OK);
    assert(procedural.vertexCount == 0 && procedural.indexCount == 0);
    assert(bl_freeGeometryData(runtime, &procedural) == BL_OK);
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
    const bl_DirectionalLightOptions directionalOptions{{0, -1, 0}, {}};
    bl_DirectionalLight directional{};
    assert(bl_createDirectionalLight(runtime, &directionalOptions, &directional) == BL_OK);
    bl_Light lightFamily{};
    assert(bl_directionalLightAsLight(directional, &lightFamily) == BL_OK);
    assert(bl_setLightIntensity(lightFamily, -2) == BL_OK);
    bl_DirectionalLightProperties directionalProperties{};
    assert(bl_getDirectionalLightProperties(directional, &directionalProperties) == BL_OK);
    assert(directionalProperties.intensity == -2);
    bl_StandardMaterial standard{};
    assert(bl_createStandardMaterial(runtime, &standard) == BL_OK);
    assert(bl_disposeRuntime(runtime) == BL_OK);
    return 0;
}
