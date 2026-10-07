#include "babylon_lite.h"
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <windows.h>
struct AllocState { size_t live; int failAfter; size_t bytes; };
static void *allocate(void *u, size_t n, size_t a)
{
    AllocState *s = (AllocState *)u;
    if (s && s->failAfter >= 0 && !s->failAfter--) return NULL;
    void *p = _aligned_malloc(n, a); if (p && s) { ++s->live; s->bytes += n; } return p;
}
static void deallocate(void *u, void *p, size_t n, size_t)
{
    if (p && u) { --((AllocState *)u)->live; ((AllocState *)u)->bytes -= n; } _aligned_free(p);
}
struct ErrorState { bl_Runtime *other; bl_RuntimeOptions options; unsigned calls; };
static void onError(void *p, const bl_Error *error)
{
    ErrorState *state = (ErrorState *)p;
    ++state->calls;
    assert(error->status != BL_OK && error->message.length);
    bl_Error nested = {};
    assert(bl_getLastError(state->other, &nested) == BL_BUSY);
    bl_Runtime *created = NULL;
    assert(bl_createRuntime(&state->options, &created) == BL_BUSY && !created);
}
struct ThreadState { bl_TransformNode node; bl_Status result; };
static DWORD WINAPI wrongThread(void *p)
{
    ThreadState *state = (ThreadState *)p;
    bl_Vec3 position = {987, 0, 0};
    state->result = bl_getNodePosition(state->node, &position);
    assert(state->result == BL_WRONG_THREAD && position.x == 987);
    return 0;
}
int main()
{
    bl_RuntimeOptions options = {};
    options.allocator = {NULL, allocate, deallocate};
    bl_Runtime *r = NULL;
    assert(bl_createRuntime(&options, &r) == BL_OK);
    bl_GeometryData a = {}, b = {};
    assert(bl_createBoxData(r, NULL, &a) == BL_OK);
    assert(bl_createBoxData(r, NULL, &b) == BL_OK);
    assert(a.vertexCount == 24 && a.indexCount == 36);
    assert(a.positions != b.positions && a.normals != b.normals && a.uvs != b.uvs && a.indices != b.indices);
    a.positions[0] = 123;
    assert(b.positions[0] != 123);
    assert(bl_freeGeometryData(r, &a) == BL_OK && !a.allocation);
    assert(bl_freeGeometryData(r, &b) == BL_OK);
    bl_SphereOptions sphere = {};
    sphere.segments = {true, 16}; sphere.diameter = {true, 4000};
    assert(bl_createSphereData(r, &sphere, &a) == BL_OK);
    assert(a.vertexCount == 703 && a.indexCount == 3888);
    assert(bl_freeGeometryData(r, &a) == BL_OK);
    bl_TransformNode root = {}, child = {};
    assert(bl_createTransformNode(r, {"root", 4}, NULL, &root) == BL_OK);
    assert(bl_createTransformNode(r, {"child", 5}, NULL, &child) == BL_OK);
    ThreadState threadState = {root, BL_OK};
    HANDLE thread = CreateThread(NULL, 0, wrongThread, &threadState, 0, NULL);
    assert(thread && WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0 && threadState.result == BL_WRONG_THREAD);
    CloseHandle(thread);
    assert(bl_setNodePosition(root, {3, 4, 5}) == BL_OK);
    assert(bl_appendNodeChild(root, child) == BL_OK);
    bl_SceneNode parent = {};
    assert(bl_getNodeParent(child, &parent) == BL_OK && !parent._id);
    assert(bl_setNodeParent(child, root) == BL_OK);
    assert(bl_setNodeParent(root, child) == BL_INVALID_ARGUMENT);
    bl_Mat4 matrix = {}; uint64_t version = 0;
    assert(bl_getNodeWorldMatrix(child, &matrix, &version) == BL_OK);
    assert(matrix.values[12] == 3 && matrix.values[13] == 4 && matrix.values[14] == 5);
    bl_Vec3 e = {0.5, 1.5707963267948966, 0.7}, read = {};
    assert(bl_setNodeRotation(child, e) == BL_OK && bl_getNodeRotation(child, &read) == BL_OK);
    assert(read.x == e.x && read.y == e.y && read.z == e.z);
    assert(bl_setNodeScaling(child, {0, 0, 0}) == BL_OK);
    assert(bl_disposeNode(child) == BL_BUSY);
    assert(bl_setNodeParent(child, {}) == BL_OK && bl_removeNodeChild(root, child) == BL_OK);
    assert(bl_disposeNode(child) == BL_OK && bl_disposeNode(child) == BL_OK);
    assert(bl_getNodePosition(child, &read) == BL_DISPOSED);
    bl_Runtime *other = NULL;
    assert(bl_createRuntime(&options, &other) == BL_OK);
    bl_TransformNode foreign = {};
    assert(bl_createTransformNode(other, {"foreign", 7}, NULL, &foreign) == BL_OK);
    assert(bl_setNodeParent(root, foreign) == BL_WRONG_RUNTIME);
    assert(bl_disposeRuntime(other) == BL_OK);
    bl_VertexSemantic attributes[] = {BL_ATTRIBUTE_POSITION};
    double number = 16777217;
    bl_ShaderUniformDecl declarations[] = {
        {{"worldViewProjection", 19}, BL_UNIFORM_MAT4, {}, true},
        {{"counter", 7}, BL_UNIFORM_U32, {&number, 1}, false},
        {{"alphaCutoff", 11}, BL_UNIFORM_F32, {}, true}
    };
    bl_ShaderMaterialOptions materialOptions = {};
    materialOptions.vertexSource = {"@vertex fn mainVertex() {}", 26};
    materialOptions.fragmentSource = {"@fragment fn mainFragment() {}", 30};
    materialOptions.attributes = attributes; materialOptions.attributeCount = 1;
    materialOptions.uniforms = declarations; materialOptions.uniformCount = 3;
    bl_ShaderMaterial material = {};
    assert(bl_createShaderMaterial(r, &materialOptions, &material) == BL_OK);
    bl_ShaderUniformView uniform = {};
    assert(bl_getShaderUniform(material, {"counter", 7}, &uniform) == BL_OK);
    assert(uniform.values.count == 1 && uniform.values.data[0] == 16777216.0f);
    const float *backing = uniform.values.data;
    for (int i = 0; i < 100; ++i) {
        bl_TransformNode node = {};
        assert(bl_createTransformNode(r, {}, NULL, &node) == BL_OK);
    }
    assert(bl_setShaderFloat(material, {"counter", 7}, 7) == BL_OK);
    assert(backing[0] == 7);
    assert(bl_getShaderUniform(material, {"counter", 7}, &uniform) == BL_OK && uniform.values.data == backing);
    assert(bl_setShaderVector3(material, {"counter", 7}, {1, 2, 3}) == BL_INVALID_ARGUMENT);
    assert(bl_setShaderFloat(material, {"missing", 7}, 1) == BL_INVALID_ARGUMENT);
    assert(bl_getShaderUniform(material, {"alphaCutoff", 11}, &uniform) == BL_OK && uniform.values.data[0] == .4f);
    assert(bl_disposeShaderMaterial(material) == BL_OK && bl_disposeShaderMaterial(material) == BL_OK);
    assert(bl_setShaderFloat(material, {"counter", 7}, 7) == BL_DISPOSED);
    bl_AudioEngine audio = {};
    assert(bl_createAudioEngineAsync(r, NULL, &audio) == BL_AUDIO_UNAVAILABLE);
    assert(bl_disposeRuntime(r) == BL_OK);
    for (int failure = 0; failure < 12; ++failure) {
        AllocState state = {0, -1};
        options.allocator.userData = &state;
        assert(bl_createRuntime(&options, &r) == BL_OK);
        state.failAfter = failure;
        bl_GeometryData output = {}; output.vertexCount = 987;
        bl_Status status = bl_createBoxData(r, NULL, &output);
        assert(status == BL_OK || status == BL_OUT_OF_MEMORY);
        if (status == BL_OK) assert(bl_freeGeometryData(r, &output) == BL_OK);
        else assert(output.vertexCount == 987 && !output.allocation);
        state.failAfter = -1;
        assert(bl_disposeRuntime(r) == BL_OK);
        assert(state.live == 0);
    }
    AllocState churn = {0, -1, 0};
    options.allocator.userData = &churn;
    assert(bl_createRuntime(&options, &r) == BL_OK);
    size_t geometryPlateau = 0;
    for (int i = 0; i < 10000; ++i) {
        bl_GeometryData data = {};
        assert(bl_createBoxData(r, NULL, &data) == BL_OK);
        assert(bl_freeGeometryData(r, &data) == BL_OK);
        if (!i) geometryPlateau = churn.bytes;
        assert(churn.bytes == geometryPlateau);
    }
    bl_GeometryData original = {}, current = {};
    assert(bl_createBoxData(r, NULL, &original) == BL_OK);
    bl_GeometryData stale = original;
    assert(bl_freeGeometryData(r, &original) == BL_OK);
    assert(bl_createBoxData(r, NULL, &current) == BL_OK);
    assert(bl_freeGeometryData(r, &stale) == BL_INVALID_HANDLE);
    assert(current.vertexCount == 24 && current.positions);
    assert(bl_freeGeometryData(r, &current) == BL_OK);
    size_t nodePlateau = 0;
    bl_TransformNode staleNode = {};
    for (int i = 0; i < 10000; ++i) {
        bl_TransformNode node = {};
        assert(bl_createTransformNode(r, {}, NULL, &node) == BL_OK);
        if (!i) staleNode = node;
        assert(bl_disposeNode(node) == BL_OK);
        if (!i) nodePlateau = churn.bytes;
        assert(churn.bytes == nodePlateau);
    }
    assert(bl_getNodePosition(staleNode, &read) == BL_DISPOSED);
    assert(bl_disposeNode(staleNode) == BL_OK);
    printf("Geometry 10000-cycle live-byte plateau: %zu; node 10000-cycle plateau: %zu\n", geometryPlateau, nodePlateau);
    assert(bl_disposeRuntime(r) == BL_OK && churn.live == 0 && churn.bytes == 0);
    ErrorState errorState = {};
    errorState.options.allocator = {NULL, allocate, deallocate};
    assert(bl_createRuntime(&errorState.options, &errorState.other) == BL_OK);
    errorState.options.onError = onError;
    errorState.options.errorUserData = &errorState;
    assert(bl_createRuntime(&errorState.options, &r) == BL_OK);
    bl_ShaderMaterialOptions invalid = {};
    assert(bl_createShaderMaterial(r, &invalid, &material) == BL_INVALID_ARGUMENT);
    assert(errorState.calls == 1);
    AllocState startupFailure = {0, 0, 0};
    bl_RuntimeOptions failingOptions = errorState.options;
    failingOptions.allocator.userData = &startupFailure;
    bl_Runtime *failedRuntime = NULL;
    assert(bl_createRuntime(&failingOptions, &failedRuntime) == BL_OUT_OF_MEMORY && !failedRuntime);
    assert(errorState.calls == 2 && startupFailure.live == 0);
    assert(bl_disposeRuntime(r) == BL_OK && bl_disposeRuntime(errorState.other) == BL_OK);
    puts("Independent CPU lifecycle/geometry/hierarchy fixtures passed");
    return 0;
}
