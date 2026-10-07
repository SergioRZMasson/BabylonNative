#include "TestRuntime.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace
{
    struct Allocations
    {
        std::unordered_map<void*, size_t> live;
        size_t bytes{};
        size_t peak{};

        static void* Allocate(void* user, size_t bytes, size_t alignment)
        {
            auto& allocations = *static_cast<Allocations*>(user);
            void* memory = TestRuntime::Allocate(nullptr, bytes, alignment);
            if (memory)
            {
                allocations.live.emplace(memory, bytes);
                allocations.bytes += bytes;
                allocations.peak = std::max(allocations.peak, allocations.bytes);
            }
            return memory;
        }

        static void Deallocate(void* user, void* memory, size_t, size_t)
        {
            auto& allocations = *static_cast<Allocations*>(user);
            const auto found = allocations.live.find(memory);
            if (found != allocations.live.end())
            {
                allocations.bytes -= found->second;
                allocations.live.erase(found);
            }
            else
            {
                ADD_FAILURE() << "Native allocator freed an unknown pointer.";
            }
            TestRuntime::Deallocate(nullptr, memory, 0, 0);
        }
    };
}

TEST(LiteRuntime, GeometryAllocationAndRegistryBytesPlateauAfterWarmup)
{
    Allocations allocations;
    bl_RuntimeOptions options{};
    options.allocator = {&allocations, Allocations::Allocate, Allocations::Deallocate};
    bl_Runtime* runtime{};
    ASSERT_EQ(bl_createRuntime(&options, &runtime), BL_OK);
    for (uint32_t i = 0; i < 64; ++i)
    {
        bl_GeometryData data{};
        ASSERT_EQ(bl_createBoxData(runtime, nullptr, &data), BL_OK);
        ASSERT_EQ(bl_freeGeometryData(runtime, &data), BL_OK);
    }
    const size_t warmBytes = allocations.bytes;
    const size_t warmPeak = allocations.peak;
    for (uint32_t i = 0; i < 1024; ++i)
    {
        bl_GeometryData data{};
        ASSERT_EQ(bl_createBoxData(runtime, nullptr, &data), BL_OK);
        ASSERT_EQ(bl_freeGeometryData(runtime, &data), BL_OK);
    }
    EXPECT_LE(allocations.bytes, warmBytes + 512u);
    EXPECT_LE(allocations.peak, warmPeak + 512u);
    EXPECT_EQ(bl_disposeRuntime(runtime), BL_OK);
    EXPECT_EQ(allocations.bytes, 0u);
    EXPECT_TRUE(allocations.live.empty());
}

TEST(LiteRuntime, RecycledNodeSlotsDoNotAliasStaleTypedHandlesOrGrowLinearly)
{
    Allocations allocations;
    bl_RuntimeOptions options{};
    options.allocator = {&allocations, Allocations::Allocate, Allocations::Deallocate};
    bl_Runtime* runtime{};
    ASSERT_EQ(bl_createRuntime(&options, &runtime), BL_OK);
    bl_TransformNode stale{};
    ASSERT_EQ(bl_createTransformNode(runtime, {"stale", 5}, nullptr, &stale), BL_OK);
    ASSERT_EQ(bl_disposeNode(stale), BL_OK);
    for (uint32_t i = 0; i < 64; ++i)
    {
        bl_TransformNode node{};
        ASSERT_EQ(bl_createTransformNode(runtime, {"fresh", 5}, nullptr, &node), BL_OK);
        ASSERT_EQ(bl_disposeNode(node), BL_OK);
    }
    const size_t warmBytes = allocations.bytes;
    const size_t warmPeak = allocations.peak;
    for (uint32_t i = 0; i < 1024; ++i)
    {
        bl_TransformNode node{};
        ASSERT_EQ(bl_createTransformNode(runtime, {"fresh", 5}, nullptr, &node), BL_OK);
        EXPECT_NE(std::memcmp(&stale, &node, sizeof(node)), 0);
        bl_Vec3 position{};
        const auto status = bl_getNodePosition(stale, &position);
        EXPECT_TRUE(status == BL_DISPOSED || status == BL_INVALID_HANDLE);
        ASSERT_EQ(bl_setNodePosition(node, {static_cast<double>(i), 0, 0}), BL_OK);
        ASSERT_EQ(bl_getNodePosition(node, &position), BL_OK);
        EXPECT_EQ(position.x, i);
        ASSERT_EQ(bl_disposeNode(node), BL_OK);
    }
    EXPECT_LE(allocations.bytes, warmBytes + 512u);
    EXPECT_LE(allocations.peak, warmPeak + 512u);
    EXPECT_EQ(bl_disposeRuntime(runtime), BL_OK);
    EXPECT_TRUE(allocations.live.empty());
}

TEST(LiteRuntime, DataOnlyRuntimeNeedsNoRendererStateServiceButEngineRequiresIt)
{
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    bl_FreeCamera camera{};
    ASSERT_EQ(bl_createFreeCamera(runtime.runtime, {}, {0, 0, 1}, &camera), BL_OK);
    bl_NativeEngineOptions native{};
    native.ownership = BL_BGFX_OWNED;
    native.backend = BL_RENDERER_D3D11;
    native.target = {BL_TARGET_SWAPCHAIN, BL_INVALID_BGFX_HANDLE, 64, 64, BL_COLOR_RGBA8, BL_DEPTH_D24S8, 1, 0, 8};
    bl_EngineContext engine{};
    EXPECT_EQ(bl_createEngine(runtime.runtime, &native, nullptr, &engine), BL_INVALID_ARGUMENT);
    native.ownership = BL_BGFX_BORROWED;
    native.isExternalBgfxInitialized = [](void*) { return false; };
    native.waitForSubmittedWork = [](void*) { return BL_OK; };
    EXPECT_EQ(bl_createEngine(runtime.runtime, &native, nullptr, &engine), BL_NOT_READY);
}
