#include "TestRuntime.h"
#include "GeometryDataInternal.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
    struct OriginalFrameCase
    {
        const char* directory;
        bl_Vec3Span path;
    };
#include "Scene38Frames.inc"

    void CompareFrame(const OriginalFrameCase& test,const char* stream,const void* data,size_t bytes)
    {
        std::ifstream file(std::filesystem::path(BL_ORACLE_DIRECTORY)/"Scene38"/test.directory/
            (std::string(stream)+".bin"),std::ios::binary|std::ios::ate);
        ASSERT_TRUE(file);
        ASSERT_EQ(static_cast<size_t>(file.tellg()),bytes);
        std::vector<unsigned char> expected(bytes);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(expected.data()),static_cast<std::streamsize>(bytes));
        ASSERT_TRUE(file);
        EXPECT_EQ(std::memcmp(data,expected.data(),bytes),0) << test.directory << "/" << stream
            << " original F64 stages, including signed zeros";
    }
}

TEST(LiteProceduralFrames, OriginalF64DuplicateZeroReversalVerticalEpsilonAndNonuniformFrames)
{
    TestRuntime runtime;
    for(const auto& test:originalFrameCases)
    {
        L_Path3D frame{};
        ASSERT_EQ(l_computePath3D(runtime.runtime,test.path,&frame),BL_OK);
        CompareFrame(test,"tangents",frame.tangents,test.path.count*sizeof(bl_Vec3));
        CompareFrame(test,"normals",frame.normals,test.path.count*sizeof(bl_Vec3));
        CompareFrame(test,"binormals",frame.binormals,test.path.count*sizeof(bl_Vec3));
        CompareFrame(test,"distances",frame.distances,test.path.count*sizeof(double));
        l_freePath3D(runtime.runtime,&frame);
        EXPECT_EQ(frame.tangents,nullptr);
    }
}
