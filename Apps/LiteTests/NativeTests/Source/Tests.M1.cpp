#include "TestRuntime.h"
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
    template<typename T>
    std::vector<T> M1Golden(const std::string& name)
    {
        std::ifstream file(std::filesystem::path(BL_ORACLE_DIRECTORY) / "M1" / name,
            std::ios::binary | std::ios::ate);
        EXPECT_TRUE(file) << name;
        if (!file)
            return {};
        const auto count = static_cast<size_t>(file.tellg()) / sizeof(T);
        std::vector<T> bytes(count);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(count * sizeof(T)));
        EXPECT_TRUE(file);
        return bytes;
    }

    void Compare(const char* name, const bl_GeometryData& data)
    {
        const auto check = [name](const char* stream, const auto* actual, size_t count) {
            using T = std::remove_cv_t<std::remove_pointer_t<decltype(actual)>>;
            const auto expected = M1Golden<T>(std::string(name) + "-" + stream + ".bin");
            ASSERT_EQ(expected.size(), count);
            EXPECT_EQ(std::memcmp(expected.data(), actual, count * sizeof(T)), 0) << name << stream;
        };
        check("positions", data.positions, data.vertexCount * 3);
        check("normals", data.normals, data.vertexCount * 3);
        check("uvs", data.uvs, data.vertexCount * 2);
        check("indices", data.indices, data.indexCount);
    }
}

TEST(LiteM1Geometry, OriginalTorusTypedArrayGoldensAndIndependentOwnership)
{
    TestRuntime r;
    const char* names[] = {"torus-default", "torus-one", "torus-zero", "torus-negative", "torus-scene162"};
    const bl_TorusOptions options[] = {{}, {{}, {}, {true, 1}}, {{true, 0}, {true, 0}, {true, 2}},
        {{true, -2}, {true, .7}, {true, 7}}, {{true, 2}, {true, .45}, {true, 64}}};
    for (size_t i = 0; i < std::size(names); ++i)
    {
        bl_GeometryData data{};
        bl_GeometryData other{};
        ASSERT_EQ(bl_createTorusData(r.runtime, &options[i], &data), BL_OK);
        Compare(names[i], data);
        ASSERT_EQ(bl_createTorusData(r.runtime, &options[i], &other), BL_OK);
        EXPECT_NE(data.positions, other.positions);
        ASSERT_EQ(bl_freeGeometryData(r.runtime, &data), BL_OK);
        ASSERT_EQ(bl_freeGeometryData(r.runtime, &other), BL_OK);
    }
    bl_GeometryData output{};
    output.vertexCount = 99;
    for (double invalid : {0.0, -1.0, 1.5, 4294967295.0, double(INFINITY), double(NAN)})
    {
        bl_TorusOptions bad{};
        bad.tessellation = {true, invalid};
        EXPECT_EQ(bl_createTorusData(r.runtime, &bad, &output), BL_INVALID_ARGUMENT);
        EXPECT_EQ(output.vertexCount, 99u);
    }
}

TEST(LiteM1Geometry, OriginalHeightMapLuminanceF32NormalsHostReleaseAndFailure)
{
    struct Host
    {
        unsigned buffers{};
        unsigned images{};
        bool fail{};
        uint8_t pixels[32] = {255, 0, 0, 0, 0, 255, 0, 255, 0, 0, 255, 0, 99, 99, 99, 99,
            255, 255, 255, 255, 0, 0, 0, 255, 50, 100, 150, 0, 99, 99, 99, 99};
    } host;
    bl_IOService io{};
    io.userData = &host;
    io.readFile = [](void*, bl_String, bl_HostBuffer* out) {
        static const uint8_t bytes[] = {42};
        *out = {bytes, sizeof(bytes), nullptr};
        return BL_OK;
    };
    io.releaseBuffer = [](void* user, bl_HostBuffer*) { ++static_cast<Host*>(user)->buffers; };
    io.decodeImage = [](void* user, bl_Bytes, bl_HostImage* out) {
        auto& state = *static_cast<Host*>(user);
        *out = {state.pixels, 3, 2, 16, nullptr};
        return state.fail ? BL_HOST_ERROR : BL_OK;
    };
    io.releaseImage = [](void* user, bl_HostImage*) { ++static_cast<Host*>(user)->images; };
    bl_RuntimeOptions options{};
    options.allocator = {nullptr, TestRuntime::Allocate, TestRuntime::Deallocate};
    options.io = &io;
    bl_Runtime* runtime{};
    ASSERT_EQ(bl_createRuntime(&options, &runtime), BL_OK);
    const bl_HeightMapGroundOptions ground{
        {{true, 3}, {true, 5}, {true, 3}, true, {1.3, -2.2}}, {true, -2}, {true, 5}};
    bl_GeometryData data{};
    ASSERT_EQ(bl_createGroundFromHeightMapData(runtime, {"oracle", 6}, &ground, &data), BL_OK);
    Compare("heightmap", data);
    EXPECT_EQ(host.buffers, 1u);
    EXPECT_EQ(host.images, 1u);
    ASSERT_EQ(bl_freeGeometryData(runtime, &data), BL_OK);
    host.fail = true;
    data.vertexCount = 99;
    EXPECT_EQ(bl_createGroundFromHeightMapData(runtime, {"oracle", 6}, &ground, &data), BL_HOST_ERROR);
    EXPECT_EQ(data.vertexCount, 99u);
    EXPECT_EQ(host.buffers, 2u);
    EXPECT_EQ(host.images, 2u);
    EXPECT_EQ(bl_disposeRuntime(runtime), BL_OK);
}

TEST(LiteM1Nodes, OriginalF32AndF64ComposeGoldensAndAtomicErrors)
{
    bl_Mat4 matrix{};
    for (bool precise : {false, true})
    {
        ASSERT_EQ(bl_composeMat4(5000000.123456, -2.7, -5000000.654321, .1, -.2, .3, .9,
                      -2, 0, 3.7, precise, &matrix),
            BL_OK);
        if (precise)
        {
            const auto expected = M1Golden<double>("compose-f64.bin");
            ASSERT_EQ(expected.size(), 16u);
            EXPECT_EQ(std::memcmp(expected.data(), matrix.values, sizeof(matrix)), 0);
        }
        else
        {
            const auto expected = M1Golden<float>("compose-f32.bin");
            ASSERT_EQ(expected.size(), 16u);
            for (size_t i = 0; i < 16; ++i)
                EXPECT_DOUBLE_EQ(matrix.values[i], expected[i]);
        }
    }
    const auto old = matrix;
    EXPECT_EQ(bl_createTranslationMat4(INFINITY, 0, 0, true, &matrix), BL_INVALID_ARGUMENT);
    EXPECT_EQ(std::memcmp(&old, &matrix, sizeof(matrix)), 0);
    EXPECT_EQ(bl_createIdentityMat4(nullptr), BL_INVALID_ARGUMENT);
}

TEST(LiteM1Nodes, DeepCloneAllocationFailuresLeaveTheSourceAndOutputIntact)
{
    struct Heap
    {
        int remaining{-1};
        size_t live{};
    };
    unsigned failures = 0;
    bool completed = false;
    for (int failure = 0; failure < 64; ++failure)
    {
        Heap heap;
        bl_RuntimeOptions options{};
        options.allocator.userData = &heap;
        options.allocator.allocate = [](void* user, size_t bytes, size_t alignment) -> void* {
            auto& heap = *static_cast<Heap*>(user);
            if (heap.remaining == 0)
                return nullptr;
            if (heap.remaining > 0)
                --heap.remaining;
            void* memory = TestRuntime::Allocate(nullptr, bytes, alignment);
            if (memory)
                ++heap.live;
            return memory;
        };
        options.allocator.deallocate = [](void* user, void* memory, size_t bytes, size_t alignment) {
            --static_cast<Heap*>(user)->live;
            TestRuntime::Deallocate(nullptr, memory, bytes, alignment);
        };
        bl_Runtime* runtime{};
        ASSERT_EQ(bl_createRuntime(&options, &runtime), BL_OK);
        bl_TransformNode root{};
        bl_TransformNode child{};
        ASSERT_EQ(bl_createTransformNode(runtime, {"root", 4}, nullptr, &root), BL_OK);
        ASSERT_EQ(bl_createTransformNode(runtime, {"child", 5}, nullptr, &child), BL_OK);
        ASSERT_EQ(bl_setNodePosition(child, {1, 2, 3}), BL_OK);
        ASSERT_EQ(bl_setNodeParent(child, root), BL_OK);
        ASSERT_EQ(bl_appendNodeChild(root, child), BL_OK);
        bl_SceneNode output{runtime, 123};
        heap.remaining = failure;
        const auto status = bl_cloneTransformNode(root, &output);
        heap.remaining = -1;
        if (status == BL_OUT_OF_MEMORY)
        {
            ++failures;
            EXPECT_EQ(output._id, 123u);
        }
        else
        {
            EXPECT_EQ(status, BL_OK);
            completed = status == BL_OK;
        }
        bl_NodeChildren children{};
        EXPECT_EQ(bl_getNodeChildren(root, &children), BL_OK);
        ASSERT_EQ(children.count, 1u);
        EXPECT_EQ(children.data[0]._id, child._id);
        bl_Vec3 position{};
        EXPECT_EQ(bl_getNodePosition(child, &position), BL_OK);
        EXPECT_DOUBLE_EQ(position.x, 1);
        EXPECT_EQ(bl_disposeRuntime(runtime), BL_OK);
        EXPECT_EQ(heap.live, 0u) << failure;
        if (completed)
            break;
    }
    EXPECT_TRUE(completed);
    EXPECT_GE(failures, 5u);
}

TEST(LiteM1Nodes, ReparentPreservesShearMirrorAndChildListsWithSingularFallback)
{
    TestRuntime r;
    bl_TransformNode a{};
    bl_TransformNode b{};
    bl_TransformNode child{};
    const bl_TransformNodeOptions parent{{7, -3, 2}, {0, 0, .3, .9539392014169457}, {-2, 3, 4}};
    ASSERT_EQ(bl_createTransformNode(r.runtime, {"a", 1}, nullptr, &a), BL_OK);
    ASSERT_EQ(bl_createTransformNode(r.runtime, {"b", 1}, &parent, &b), BL_OK);
    ASSERT_EQ(bl_createTransformNode(r.runtime, {"child", 5}, nullptr, &child), BL_OK);
    ASSERT_EQ(bl_setNodePosition(child, {1, 2, 3}), BL_OK);
    bl_Mat4 before{};
    uint64_t version{};
    ASSERT_EQ(bl_getNodeWorldMatrix(child, &before, &version), BL_OK);
    ASSERT_EQ(bl_setParent(child, b), BL_OK);
    bl_Mat4 after{};
    ASSERT_EQ(bl_getNodeWorldMatrix(child, &after, &version), BL_OK);
    for (unsigned i = 0; i < 16; ++i)
        EXPECT_NEAR(before.values[i], after.values[i], 1e-6);
    bl_NodeChildren children{};
    ASSERT_EQ(bl_getNodeChildren(b, &children), BL_OK);
    ASSERT_EQ(children.count, 1u);
    ASSERT_EQ(bl_setParent(child, b), BL_OK);
    ASSERT_EQ(bl_getNodeChildren(b, &children), BL_OK);
    EXPECT_EQ(children.count, 1u);
    ASSERT_EQ(bl_setParent(child, a), BL_OK);
    ASSERT_EQ(bl_getNodeChildren(b, &children), BL_OK);
    EXPECT_EQ(children.count, 0u);
    EXPECT_EQ(bl_setParent(a, child), BL_INVALID_ARGUMENT);
    ASSERT_EQ(bl_setNodeScaling(b, {0, 1, 1}), BL_OK);
    ASSERT_EQ(bl_getNodeWorldMatrix(child, &before, &version), BL_OK);
    ASSERT_EQ(bl_setParent(child, b), BL_OK);
    bl_Vec3 position{};
    ASSERT_EQ(bl_getNodePosition(child, &position), BL_OK);
    EXPECT_DOUBLE_EQ(position.x, before.values[12]);
    EXPECT_DOUBLE_EQ(position.y, before.values[13]);
    EXPECT_DOUBLE_EQ(position.z, before.values[14]);
}

TEST(LiteM1Nodes, OrthographicUpdatesNeverInvalidateCameraWorld)
{
    TestRuntime r;
    bl_FreeCamera camera{};
    ASSERT_EQ(bl_createFreeCamera(r.runtime, {0, 1, -3}, {}, &camera), BL_OK);
    bl_SceneNode node{};
    bl_Camera family{};
    ASSERT_EQ(bl_cameraNode(camera, &node), BL_OK);
    ASSERT_EQ(bl_freeCameraAsCamera(camera, &family), BL_OK);
    bl_Mat4 world{};
    uint64_t before{};
    uint64_t after{};
    ASSERT_EQ(bl_getNodeWorldMatrix(node, &world, &before), BL_OK);
    ASSERT_EQ(bl_enableOrthographicCamera(family, nullptr), BL_OK);
    bl_OrthographicBounds bounds{};
    ASSERT_EQ(bl_getOrthographicBounds(family, &bounds), BL_OK);
    bounds.halfHeight = 7;
    ASSERT_EQ(bl_setOrthographicBounds(family, &bounds), BL_OK);
    ASSERT_EQ(bl_getNodeWorldMatrix(node, &world, &after), BL_OK);
    EXPECT_EQ(before, after);
    bounds.halfHeight = 0;
    EXPECT_EQ(bl_setOrthographicBounds(family, &bounds), BL_INVALID_ARGUMENT);
    ASSERT_EQ(bl_getOrthographicBounds(family, &bounds), BL_OK);
    EXPECT_DOUBLE_EQ(bounds.halfHeight, 7);
    EXPECT_EQ(bl_disableOrthographicCamera(family), BL_OK);
    EXPECT_EQ(bl_getOrthographicBounds(family, &bounds), BL_NOT_READY);
}

TEST(LiteM1Nodes, DetachedDeepCloneHasIndependentChildrenAndOriginalVisibilityRules)
{
    TestRuntime r;
    bl_TransformNode source{};
    bl_TransformNode child{};
    ASSERT_EQ(bl_createTransformNode(r.runtime, {"source", 6}, nullptr, &source), BL_OK);
    ASSERT_EQ(bl_createTransformNode(r.runtime, {"child", 5}, nullptr, &child), BL_OK);
    ASSERT_EQ(bl_setNodePosition(source, {1, 2, 3}), BL_OK);
    ASSERT_EQ(bl_setNodeVisible(source, false), BL_OK);
    ASSERT_EQ(bl_setNodeParent(child, source), BL_OK);
    ASSERT_EQ(bl_appendNodeChild(source, child), BL_OK);
    bl_SceneNode cloned{};
    ASSERT_EQ(bl_cloneTransformNode(source, &cloned), BL_OK);
    bl_NodeChildren children{};
    ASSERT_EQ(bl_getNodeChildren(cloned, &children), BL_OK);
    ASSERT_EQ(children.count, 1u);
    EXPECT_NE(children.data[0]._id, child._id);
    bl_SceneNode parent{};
    ASSERT_EQ(bl_getNodeParent(cloned, &parent), BL_OK);
    EXPECT_EQ(parent._id, 0u);
    ASSERT_EQ(bl_getNodeParent(children.data[0], &parent), BL_OK);
    EXPECT_EQ(parent._id, cloned._id);
    bool visible{};
    ASSERT_EQ(bl_getNodeVisible(cloned, &visible), BL_OK);
    EXPECT_TRUE(visible);
    bl_String name{};
    ASSERT_EQ(bl_getNodeName(cloned, &name), BL_OK);
    EXPECT_EQ(std::string(name.data, name.length), "source_clone");
    ASSERT_EQ(bl_setNodePosition(cloned, {7, 8, 9}), BL_OK);
    bl_Vec3 position{};
    ASSERT_EQ(bl_getNodePosition(source, &position), BL_OK);
    EXPECT_DOUBLE_EQ(position.x, 1);
    bl_Mesh sentinel{};
    sentinel._id = 123;
    EXPECT_EQ(bl_sceneNodeAsMesh(cloned, &sentinel), BL_INVALID_HANDLE);
    EXPECT_EQ(sentinel._id, 123u);
}

TEST(LiteM1Nodes, FreeControlOwnsCameraAndRejectsAtomicInvalidEvents)
{
    TestRuntime r;
    bl_FreeCamera camera{};
    ASSERT_EQ(bl_createFreeCamera(r.runtime, {0, 0, -3}, {}, &camera), BL_OK);
    bl_FreeControl control{};
    ASSERT_EQ(bl_attachFreeControl(camera, {}, &control), BL_OK);
    bl_ArcRotateInputEffects effects{};
    bl_FreeInput input{};
    input.kind = BL_FREE_POINTER_DOWN;
    input.pointerId = 37;
    ASSERT_EQ(bl_processFreeInput(control, &input, &effects), BL_OK);
    EXPECT_TRUE(effects.capturePointer);
    EXPECT_EQ(effects.pointerId, 37);
    const auto previous = effects;
    input.kind = BL_FREE_POINTER_MOVE;
    input.clientX = INFINITY;
    EXPECT_EQ(bl_processFreeInput(control, &input, &effects), BL_INVALID_ARGUMENT);
    EXPECT_EQ(std::memcmp(&previous, &effects, sizeof(effects)), 0);
    bl_SceneNode node{};
    ASSERT_EQ(bl_cameraNode(camera, &node), BL_OK);
    EXPECT_EQ(bl_disposeNode(node), BL_BUSY);
    EXPECT_EQ(bl_detachFreeControl(control), BL_OK);
    EXPECT_EQ(bl_detachFreeControl(control), BL_OK);
    EXPECT_EQ(bl_processFreeInput(control, &input, &effects), BL_DISPOSED);
    EXPECT_EQ(bl_disposeNode(node), BL_OK);
}
