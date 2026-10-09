#include "TestRuntime.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
    template<typename T>
    std::vector<T> GroundGolden(const char* name, const char* stream)
    {
        std::ifstream file(std::filesystem::path(BL_ORACLE_DIRECTORY) /
            (std::string("ground-") + name + "-" + stream + ".bin"), std::ios::binary | std::ios::ate);
        if (!file)
        {
            ADD_FAILURE() << "Missing original Ground fixture " << name << "/" << stream;
            return {};
        }
        const auto bytes = file.tellg();
        std::vector<T> result(static_cast<size_t>(bytes) / sizeof(T));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(result.data()), bytes);
        return result;
    }

    struct GroundAllocations
    {
        size_t calls{};
        size_t fail{};
        size_t bytes{};
        std::unordered_map<void*, size_t> live;
        static void* Allocate(void* user, size_t bytes, size_t alignment)
        {
            auto& state = *static_cast<GroundAllocations*>(user);
            if (++state.calls == state.fail)
            {
                return nullptr;
            }
            auto* memory = TestRuntime::Allocate(nullptr, bytes, alignment);
            if (memory)
            {
                state.live.emplace(memory, bytes);
                state.bytes += bytes;
            }
            return memory;
        }
        static void Free(void* user, void* memory, size_t bytes, size_t alignment)
        {
            auto& state = *static_cast<GroundAllocations*>(user);
            EXPECT_EQ(state.live.erase(memory), 1u);
            state.bytes -= bytes;
            TestRuntime::Deallocate(nullptr, memory, bytes, alignment);
        }
    };
}

TEST(LiteGround, SevenOriginalSourceGoldensMatchAllTypedArrayBytes)
{
    TestRuntime runtime;
    struct Case
    {
        const char* name;
        bl_GroundOptions options;
    };
    const Case cases[] = {
        {"default", {}},
        {"primitives-8x8", {{true, 8}, {true, 8}, {}, false, {}}},
        {"subdiv-2", {{true, 3}, {true, 5}, {true, 2}, false, {}}},
        {"subdiv-3-uv", {{true, 2.5}, {true, 7.3}, {true, 3}, true, {1.3, -2.2}}},
        {"zero-width", {{true, 0}, {}, {}, false, {}}},
        {"negative-width", {{true, -2}, {true, 3}, {}, false, {}}},
        {"ignored-heightmap-only", {}},
    };
    for (const auto& test : cases)
    {
        bl_GeometryData data{};
        ASSERT_EQ(bl_createFlatGroundData(runtime.runtime, &test.options, &data), BL_OK);
        const auto positions = GroundGolden<float>(test.name, "positions");
        const auto normals = GroundGolden<float>(test.name, "normals");
        const auto uvs = GroundGolden<float>(test.name, "uvs");
        const auto indices = GroundGolden<uint32_t>(test.name, "indices");
        ASSERT_EQ(data.vertexCount * 3, positions.size());
        ASSERT_EQ(data.indexCount, indices.size());
        EXPECT_EQ(std::memcmp(data.positions, positions.data(), positions.size() * 4), 0);
        EXPECT_EQ(std::memcmp(data.normals, normals.data(), normals.size() * 4), 0);
        EXPECT_EQ(std::memcmp(data.uvs, uvs.data(), uvs.size() * 4), 0);
        EXPECT_EQ(std::memcmp(data.indices, indices.data(), indices.size() * 4), 0);
        EXPECT_EQ(bl_freeGeometryData(runtime.runtime, &data), BL_OK);
    }
}

TEST(LiteGround, DefaultsFreshStorageSignedZerosAndAbsentPoison)
{
    TestRuntime runtime;
    bl_GeometryData first{};
    bl_GeometryData second{};
    bl_GroundOptions options{};
    options.width.value = std::numeric_limits<double>::quiet_NaN();
    options.uvScale = {INFINITY, NAN};
    ASSERT_EQ(bl_createFlatGroundData(runtime.runtime, nullptr, &first), BL_OK);
    ASSERT_EQ(bl_createFlatGroundData(runtime.runtime, &options, &second), BL_OK);
    EXPECT_EQ(first.vertexCount, 4u);
    EXPECT_EQ(first.indexCount, 6u);
    EXPECT_NE(first.positions, second.positions);
    EXPECT_NE(first.normals, second.normals);
    EXPECT_NE(first.uvs, second.uvs);
    EXPECT_NE(first.indices, second.indices);
    EXPECT_EQ(std::memcmp(first.positions, second.positions, 12 * 4), 0);
    first.positions[0] = 123;
    EXPECT_EQ(second.positions[0], -.5f);
    EXPECT_EQ(bl_freeGeometryData(runtime.runtime, &first), BL_OK);
    EXPECT_EQ(bl_freeGeometryData(runtime.runtime, &second), BL_OK);
    options.hasUvScale = true;
    options.uvScale = {-0.0, -2};
    ASSERT_EQ(bl_createFlatGroundData(runtime.runtime, &options, &first), BL_OK);
    EXPECT_TRUE(std::signbit(first.uvs[0]));
    EXPECT_TRUE(std::signbit(first.uvs[5]));
    EXPECT_FALSE(std::signbit(first.positions[1]));
    EXPECT_FALSE(std::signbit(first.normals[0]));
    EXPECT_EQ(bl_freeGeometryData(runtime.runtime, &first), BL_OK);
}

TEST(LiteGround, InvalidNumbersCountsMissingOutputsAndThreadAreAtomic)
{
    TestRuntime runtime;
    bl_GeometryData sentinel{};
    sentinel.vertexCount = 123;
    const auto original = sentinel;
    for (const double value : {0.0, -1.0, 1.5, double(INFINITY), double(NAN), 65535.0,
                              double(std::numeric_limits<size_t>::max())})
    {
        bl_GroundOptions options{};
        options.subdivisions = {true, value};
        EXPECT_EQ(bl_createFlatGroundData(runtime.runtime, &options, &sentinel), BL_INVALID_ARGUMENT);
        EXPECT_EQ(std::memcmp(&sentinel, &original, sizeof(sentinel)), 0);
    }
    for (const double value : {double(INFINITY), double(NAN), std::numeric_limits<double>::max()})
    {
        bl_GroundOptions options{};
        options.width = {true, value};
        EXPECT_EQ(bl_createFlatGroundData(runtime.runtime, &options, &sentinel), BL_INVALID_ARGUMENT);
        EXPECT_EQ(std::memcmp(&sentinel, &original, sizeof(sentinel)), 0);
        options.width = {};
        options.hasUvScale = true;
        options.uvScale = {value, 1};
        EXPECT_EQ(bl_createFlatGroundData(runtime.runtime, &options, &sentinel), BL_INVALID_ARGUMENT);
    }
    EXPECT_EQ(bl_createFlatGroundData(runtime.runtime, nullptr, nullptr), BL_INVALID_ARGUMENT);
    bl_Status status{};
    std::thread worker([&] { status = bl_createFlatGroundData(runtime.runtime, nullptr, &sentinel); });
    worker.join();
    EXPECT_EQ(status, BL_WRONG_THREAD);
    EXPECT_EQ(std::memcmp(&sentinel, &original, sizeof(sentinel)), 0);
}

TEST(LiteGround, EveryAllocatorFailureCleansUpAndChurnPlateaus)
{
    for (size_t failure = 1; failure <= 8; ++failure)
    {
        GroundAllocations state;
        bl_RuntimeOptions options{};
        options.allocator = {&state, GroundAllocations::Allocate, GroundAllocations::Free};
        bl_Runtime* runtime{};
        ASSERT_EQ(bl_createRuntime(&options, &runtime), BL_OK);
        state.fail = state.calls + failure;
        bl_GeometryData data{};
        data.vertexCount = 123;
        const auto original = data;
        const auto status = bl_createFlatGroundData(runtime, nullptr, &data);
        EXPECT_TRUE(status == BL_OK || status == BL_OUT_OF_MEMORY);
        if (status == BL_OK)
        {
            EXPECT_EQ(bl_freeGeometryData(runtime, &data), BL_OK);
        }
        else
        {
            EXPECT_EQ(std::memcmp(&data, &original, sizeof(data)), 0);
        }
        state.fail = 0;
        for (size_t i = 0; i < 64; ++i)
        {
            ASSERT_EQ(bl_createFlatGroundData(runtime, nullptr, &data), BL_OK);
            ASSERT_EQ(bl_freeGeometryData(runtime, &data), BL_OK);
        }
        const size_t warm = state.bytes;
        for (size_t i = 0; i < 1024; ++i)
        {
            ASSERT_EQ(bl_createFlatGroundData(runtime, nullptr, &data), BL_OK);
            ASSERT_EQ(bl_freeGeometryData(runtime, &data), BL_OK);
        }
        EXPECT_LE(state.bytes, warm + 512);
        EXPECT_EQ(bl_disposeRuntime(runtime), BL_OK);
        EXPECT_TRUE(state.live.empty());
    }
}

TEST(LiteGround, ForeignTokensAndAllocatorReentryDoNotPublishData)
{
    TestRuntime first;
    TestRuntime second;
    bl_GeometryData data{};
    ASSERT_EQ(bl_createFlatGroundData(first.runtime, nullptr, &data), BL_OK);
    const auto original = data;
    EXPECT_EQ(bl_freeGeometryData(second.runtime, &data), BL_INVALID_HANDLE);
    EXPECT_EQ(std::memcmp(&data, &original, sizeof(data)), 0);
    ASSERT_EQ(bl_freeGeometryData(first.runtime, &data), BL_OK);
    struct Reentrant
    {
        bl_Runtime* runtime{};
        bl_Status nested{};
        bool active{};
    } state;
    bl_RuntimeOptions options{};
    options.allocator.userData = &state;
    options.allocator.allocate = [](void* user, size_t bytes, size_t alignment) {
        auto& value = *static_cast<Reentrant*>(user);
        if (value.active)
        {
            bl_GeometryData sentinel{};
            sentinel.vertexCount = 123;
            value.nested = bl_createFlatGroundData(value.runtime, nullptr, &sentinel);
            EXPECT_EQ(sentinel.vertexCount, 123u);
        }
        return TestRuntime::Allocate(nullptr, bytes, alignment);
    };
    options.allocator.deallocate = [](void*, void* memory, size_t bytes, size_t alignment) {
        TestRuntime::Deallocate(nullptr, memory, bytes, alignment);
    };
    ASSERT_EQ(bl_createRuntime(&options, &state.runtime), BL_OK);
    state.active = true;
    ASSERT_EQ(bl_createFlatGroundData(state.runtime, nullptr, &data), BL_OK);
    EXPECT_EQ(state.nested, BL_BUSY);
    ASSERT_EQ(bl_freeGeometryData(state.runtime, &data), BL_OK);
    ASSERT_EQ(bl_disposeRuntime(state.runtime), BL_OK);
}

TEST(LiteGround, MisalignedAllocationIsRejectedAndRuntimeCanRetry)
{
    struct Allocator
    {
        void* base{};
        bool poison{};
    } state;
    bl_RuntimeOptions options{};
    options.allocator.userData = &state;
    options.allocator.allocate = [](void* user, size_t bytes, size_t alignment) -> void* {
        auto& value = *static_cast<Allocator*>(user);
        if (!value.poison)
        {
            return TestRuntime::Allocate(nullptr, bytes, alignment);
        }
        value.base = TestRuntime::Allocate(nullptr, bytes + alignment, alignment);
        return static_cast<unsigned char*>(value.base) + 1;
    };
    options.allocator.deallocate = [](void* user, void* memory, size_t bytes, size_t alignment) {
        auto& value = *static_cast<Allocator*>(user);
        if (value.poison)
        {
            EXPECT_EQ(memory, static_cast<unsigned char*>(value.base) + 1);
            TestRuntime::Deallocate(nullptr, value.base, bytes + alignment, alignment);
            value.base = nullptr;
            return;
        }
        TestRuntime::Deallocate(nullptr, memory, bytes, alignment);
    };
    bl_Runtime* runtime{};
    ASSERT_EQ(bl_createRuntime(&options, &runtime), BL_OK);
    state.poison = true;
    bl_GeometryData data{};
    data.vertexCount = 123;
    const auto original = data;
    EXPECT_EQ(bl_createFlatGroundData(runtime, nullptr, &data), BL_OUT_OF_MEMORY);
    EXPECT_EQ(std::memcmp(&data, &original, sizeof(data)), 0);
    EXPECT_EQ(state.base, nullptr);
    state.poison = false;
    ASSERT_EQ(bl_createFlatGroundData(runtime, nullptr, &data), BL_OK);
    ASSERT_EQ(bl_freeGeometryData(runtime, &data), BL_OK);
    ASSERT_EQ(bl_disposeRuntime(runtime), BL_OK);
}
