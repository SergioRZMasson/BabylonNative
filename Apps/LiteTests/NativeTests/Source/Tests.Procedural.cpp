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
    struct ProceduralCase
    {
        const char* name;
        unsigned family;
        const void* options;
        const char* directory;
        bool accepted;
        size_t vertices;
        size_t indices;
    };

#include "Scene38Cases.inc"

    bl_Status Create(bl_Runtime* runtime, const ProceduralCase& test, bl_GeometryData* data)
    {
        switch (test.family)
        {
            case 0:
                return bl_createCylinderData(runtime, static_cast<const bl_CylinderOptions*>(test.options), data);
            case 1:
                return bl_createPlaneData(runtime, static_cast<const bl_PlaneOptions*>(test.options), data);
            case 2:
                return bl_createDiscData(runtime, static_cast<const bl_DiscOptions*>(test.options), data);
            case 3:
                return bl_createPolyhedronData(runtime, static_cast<const bl_PolyhedronOptions*>(test.options), data);
            case 4:
                return bl_createRibbonData(runtime, static_cast<const bl_RibbonOptions*>(test.options), data);
            case 5:
                return bl_createTubeData(runtime, static_cast<const bl_TubeOptions*>(test.options), data);
            case 6:
                return bl_createExtrudeShapeData(runtime, static_cast<const bl_ExtrudeShapeOptions*>(test.options), data);
            default:
                return BL_INVALID_ARGUMENT;
        }
    }

    template<typename T>
    void CompareStream(const ProceduralCase& test, const char* stream, const T* data, size_t count)
    {
        std::ifstream file(std::filesystem::path(BL_ORACLE_DIRECTORY) / "Scene38" / test.directory /
            (std::string(stream) + ".bin"), std::ios::binary | std::ios::ate);
        ASSERT_TRUE(file) << test.name << "/" << stream;
        ASSERT_EQ(static_cast<size_t>(file.tellg()), count * sizeof(T)) << test.name << "/" << stream;
        std::vector<T> expected(count);
        file.seekg(0);
        if (count)
        {
            file.read(reinterpret_cast<char*>(expected.data()), static_cast<std::streamsize>(count * sizeof(T)));
            ASSERT_TRUE(file);
            ASSERT_NE(data, nullptr);
            size_t mismatches{};
            for (size_t index = 0; index < count; ++index)
            {
                if (std::memcmp(data + index, expected.data() + index, sizeof(T)))
                {
                    if (mismatches < 8)
                    {
                        ADD_FAILURE() << test.name << "/" << stream << " bit mismatch at" << index
                            << " actual" << data[index] << " original" << expected[index];
                    }
                    ++mismatches;
                }
            }
            EXPECT_EQ(mismatches, 0u) << test.name << "/" << stream;
        }
    }
}

TEST(LiteProcedural, AllOriginalScene38DefaultsOptionsAndSourceDomains)
{
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    for (const auto& test : proceduralCases)
    {
        SCOPED_TRACE(test.name);
        bl_GeometryData data{};
        data.vertexCount = 987;
        const auto sentinel = data;
        const auto status = Create(runtime.runtime, test, &data);
        if (!test.accepted)
        {
            EXPECT_EQ(status, BL_INVALID_ARGUMENT);
            EXPECT_EQ(std::memcmp(&data, &sentinel, sizeof(data)), 0);
            continue;
        }
        ASSERT_EQ(status, BL_OK);
        EXPECT_EQ(data.vertexCount, test.vertices);
        EXPECT_EQ(data.indexCount, test.indices);
        CompareStream(test, "positions", data.positions, data.vertexCount * 3);
        CompareStream(test, "normals", data.normals, data.vertexCount * 3);
        CompareStream(test, "uvs", data.uvs, data.vertexCount * 2);
        CompareStream(test, "indices", data.indices, data.indexCount);
        bl_GeometryData independent{};
        ASSERT_EQ(Create(runtime.runtime, test, &independent), BL_OK);
        EXPECT_NE(data.positions, independent.positions);
        EXPECT_NE(data.normals, independent.normals);
        EXPECT_NE(data.uvs, independent.uvs);
        EXPECT_NE(data.indices, independent.indices);
        ASSERT_EQ(bl_freeGeometryData(runtime.runtime, &data), BL_OK);
        ASSERT_EQ(bl_freeGeometryData(runtime.runtime, &independent), BL_OK);
        EXPECT_EQ(data.allocation, nullptr);
    }
}

TEST(LiteProcedural, DefaultsAbsentPoisonAndUnsafeOptionsAreAtomic)
{
    TestRuntime runtime;
    bl_CylinderOptions cylinder{};
    cylinder.diameter.value = NAN;
    bl_GeometryData data{};
    ASSERT_EQ(bl_createCylinderData(runtime.runtime, &cylinder, &data), BL_OK);
    EXPECT_EQ(data.vertexCount, 102u);
    EXPECT_EQ(bl_freeGeometryData(runtime.runtime, &data), BL_OK);
    const auto check = [&](bl_Status status) {
        EXPECT_EQ(status, BL_INVALID_ARGUMENT);
        EXPECT_EQ(data.vertexCount, 987u);
        EXPECT_EQ(data.allocation, nullptr);
    };
    data.vertexCount = 987;
    cylinder.height = {true, 0};
    check(bl_createCylinderData(runtime.runtime, &cylinder, &data));
    cylinder.height = {true, INFINITY};
    check(bl_createCylinderData(runtime.runtime, &cylinder, &data));
    bl_DiscOptions disc{};
    disc.radius = {true, std::numeric_limits<double>::max()};
    check(bl_createDiscData(runtime.runtime, &disc, &data));
    disc = {};
    disc.tessellation = {true, std::numeric_limits<double>::max()};
    check(bl_createDiscData(runtime.runtime, &disc, &data));
    bl_RibbonOptions ribbon{};
    check(bl_createRibbonData(runtime.runtime, &ribbon, &data));
    bl_TubeOptions tube{};
    check(bl_createTubeData(runtime.runtime, &tube, &data));
    bl_ExtrudeShapeOptions extrude{};
    check(bl_createExtrudeShapeData(runtime.runtime, &extrude, &data));
    EXPECT_EQ(bl_createDiscData(runtime.runtime, nullptr, nullptr), BL_INVALID_ARGUMENT);
    bl_Status workerStatus{};
    std::thread worker([&] { workerStatus = bl_createDiscData(runtime.runtime, nullptr, &data); });
    worker.join();
    EXPECT_EQ(workerStatus, BL_WRONG_THREAD);
    EXPECT_EQ(data.vertexCount, 987u);
}
