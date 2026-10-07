#include "TestRuntime.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    template<typename T>
    std::vector<T> Read(const std::string& file)
    {
        std::ifstream stream(std::filesystem::path(BL_ORACLE_DIRECTORY) / file, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            ADD_FAILURE() << "Missing official-source fixture: " << file;
            return {};
        }
        const auto bytes = stream.tellg();
        if (bytes < 0 || static_cast<size_t>(bytes) % sizeof(T))
        {
            ADD_FAILURE() << "Malformed official-source fixture: " << file;
            return {};
        }
        std::vector<T> values(static_cast<size_t>(bytes) / sizeof(T));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(values.data()), bytes);
        if (!stream)
        {
            ADD_FAILURE() << "Could not read official-source fixture: " << file;
            return {};
        }
        return values;
    }

    void Compare(const std::string& name, const char* suffix, const float* actual, size_t count, bool exact)
    {
        const auto expected = Read<float>(name + "-" + suffix + ".bin");
        ASSERT_EQ(count, expected.size());
        ASSERT_NE(actual, nullptr);
        if (exact)
        {
            EXPECT_EQ(std::memcmp(actual, expected.data(), count * sizeof(float)), 0) << name << '/' << suffix << " Float32 bits";
        }
        for (size_t i = 0; i < count; ++i)
        {
            if (exact)
            {
                EXPECT_EQ(actual[i], expected[i]) << name << '/' << suffix << " element " << i;
            }
            else
            {
                const double tolerance = 2e-7 + 1e-6 * std::abs(static_cast<double>(expected[i]));
                EXPECT_NEAR(actual[i], expected[i], tolerance) << name << '/' << suffix << " element " << i;
            }
        }
    }

    void CompareGeometry(const std::string& name, const bl_GeometryData& data, bool exact)
    {
        Compare(name, "positions", data.positions, data.vertexCount * 3, exact);
        Compare(name, "normals", data.normals, data.vertexCount * 3, exact);
        Compare(name, "uvs", data.uvs, data.vertexCount * 2, exact);
        const auto indices = Read<uint32_t>(name + "-indices.bin");
        ASSERT_EQ(data.indexCount, indices.size());
        ASSERT_NE(data.indices, nullptr);
        EXPECT_EQ(std::memcmp(data.indices, indices.data(), indices.size() * sizeof(uint32_t)), 0);
    }
}

TEST(LiteGeometry, MatchesFourOfficialBoxFactoryFixturesExactly)
{
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    struct Case
    {
        const char* name;
        bl_BoxOptions options;
        bool defaults;
    };
    const Case cases[]{
        {"box-default", {}, true},
        {"box-size", {{true, 2}, {}, {}, {}}, false},
        {"box-overrides", {{true, 2}, {true, 3}, {true, 4}, {true, 5}}, false},
        {"box-zero-width", {{}, {true, 0}, {}, {}}, false},
    };
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.name);
        bl_GeometryData data{};
        ASSERT_EQ(bl_createBoxData(runtime.runtime, test.defaults ? nullptr : &test.options, &data), BL_OK);
        EXPECT_EQ(data.vertexCount, 24u);
        EXPECT_EQ(data.indexCount, 36u);
        CompareGeometry(test.name, data, true);
        ASSERT_EQ(bl_freeGeometryData(runtime.runtime, &data), BL_OK);
        EXPECT_EQ(data.allocation, nullptr);
        EXPECT_EQ(data.positions, nullptr);
        EXPECT_EQ(data.vertexCount, 0u);
    }
}

TEST(LiteGeometry, MatchesOfficialSphereAndFullMinecraftSkyFixtures)
{
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    struct Case
    {
        const char* name;
        bl_SphereOptions options;
        bool defaults;
        size_t vertices;
        size_t indices;
    };
    const Case cases[]{
        {"sphere-default", {}, true, 2415, 13872},
        {"sphere-minimum", {{true, 0}, {}, {}, {}, {}}, false, 66, 300},
        {"sphere-axes", {{true, 8}, {}, {true, 2}, {true, 3}, {true, 4}}, false, 231, 1200},
        {"sphere-minecraft-sky", {{true, 16}, {true, 4000}, {}, {}, {}}, false, 703, 3888},
    };
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.name);
        bl_GeometryData data{};
        ASSERT_EQ(bl_createSphereData(runtime.runtime, test.defaults ? nullptr : &test.options, &data), BL_OK);
        EXPECT_EQ(data.vertexCount, test.vertices);
        EXPECT_EQ(data.indexCount, test.indices);
        CompareGeometry(test.name, data, false);
        ASSERT_EQ(bl_freeGeometryData(runtime.runtime, &data), BL_OK);
    }
}

TEST(LiteGeometry, PrimitiveFactoryAllocationsAreFreshAndIndependent)
{
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    bl_GeometryData first{}, second{};
    ASSERT_EQ(bl_createBoxData(runtime.runtime, nullptr, &first), BL_OK);
    ASSERT_EQ(bl_createBoxData(runtime.runtime, nullptr, &second), BL_OK);
    EXPECT_NE(first.positions, second.positions);
    EXPECT_NE(first.normals, second.normals);
    EXPECT_NE(first.uvs, second.uvs);
    EXPECT_NE(first.indices, second.indices);
    const float unchanged = second.positions[0];
    first.positions[0] = 1234;
    EXPECT_EQ(second.positions[0], unchanged);
    EXPECT_EQ(bl_freeGeometryData(runtime.runtime, &first), BL_OK);
    EXPECT_EQ(bl_freeGeometryData(runtime.runtime, &second), BL_OK);
}
