#include "TestRuntime.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <unordered_map>

namespace
{
    struct Allocations
    {
        size_t calls{};
        size_t fail{};
        size_t bytes{};
        bool reenter{};
        bool misalign{};
        bl_Runtime* runtime{};
        bl_Status nested{};
        std::unordered_map<void*, size_t> live;
        static void* Allocate(void* user, size_t bytes, size_t alignment)
        {
            auto& value = *static_cast<Allocations*>(user);
            if (value.reenter && value.runtime)
            {
                bl_GeometryData sentinel{};
                sentinel.vertexCount = 987;
                value.nested = bl_createDiscData(value.runtime, nullptr, &sentinel);
                EXPECT_EQ(sentinel.vertexCount, 987u);
            }
            if (++value.calls == value.fail)
            {
                return nullptr;
            }
            void* pointer = TestRuntime::Allocate(nullptr, bytes + (value.misalign ? alignment : 0), alignment);
            if (pointer)
            {
                value.live.emplace(pointer, bytes);
                value.bytes += bytes;
            }
            return value.misalign && pointer ? static_cast<unsigned char*>(pointer) + 1 : pointer;
        }
        static void Free(void* user, void* memory, size_t bytes, size_t alignment)
        {
            auto& value = *static_cast<Allocations*>(user);
            void* original = value.misalign ? static_cast<unsigned char*>(memory) - 1 : memory;
            EXPECT_EQ(value.live.erase(original), 1u);
            value.bytes -= bytes;
            TestRuntime::Deallocate(nullptr, original, bytes, alignment);
        }
    };

    bl_Status Create(unsigned family, bl_Runtime* runtime, bl_GeometryData* data)
    {
        const bl_Vec3 path[]{{0,0,0},{1,.2,0},{2,.4,.2}};
        const bl_Vec3 shape[]{{-.2,-.2,0},{.2,-.2,0},{.2,.2,0},{-.2,.2,0},{-.2,-.2,0}};
        const bl_Vec3Span rows[]{{path,3},{path,3}};
        const bl_RibbonOptions ribbon{{rows,2},true,true,{}};
        const bl_TubeOptions tube{{path,3},{true,.1},{true,8},BL_CAP_ALL,{}};
        const bl_ExtrudeShapeOptions extrude{{shape,5},{path,3},{},{true,.2},BL_CAP_ALL};
        switch(family)
        {
            case 0: return bl_createCylinderData(runtime,nullptr,data);
            case 1: return bl_createPlaneData(runtime,nullptr,data);
            case 2: return bl_createDiscData(runtime,nullptr,data);
            case 3: return bl_createPolyhedronData(runtime,nullptr,data);
            case 4: return bl_createRibbonData(runtime,&ribbon,data);
            case 5: return bl_createTubeData(runtime,&tube,data);
            case 6: return bl_createExtrudeShapeData(runtime,&extrude,data);
            default: return BL_INVALID_ARGUMENT;
        }
    }
}

TEST(LiteProceduralLifetime, EveryAllocationFailureRetryAndSeventyThousandChurns)
{
    for(unsigned family=0;family<7;++family)
    {
        SCOPED_TRACE(family);
        bool reachedSuccess=false;
        for(size_t failure=1;failure<=48;++failure)
        {
            Allocations state;
            bl_RuntimeOptions options{};
            options.allocator={&state,Allocations::Allocate,Allocations::Free};
            bl_Runtime* runtime{};
            ASSERT_EQ(bl_createRuntime(&options,&runtime),BL_OK);
            state.fail=state.calls+failure;
            bl_GeometryData data{};
            data.vertexCount=987;
            const auto original=data;
            const auto result=Create(family,runtime,&data);
            if(result==BL_OK)
            {
                EXPECT_EQ(bl_freeGeometryData(runtime,&data),BL_OK);
                reachedSuccess=true;
            }
            else
            {
                EXPECT_EQ(result,BL_OUT_OF_MEMORY);
                EXPECT_EQ(std::memcmp(&data,&original,sizeof(data)),0);
            }
            state.fail=0;
            ASSERT_EQ(Create(family,runtime,&data),BL_OK);
            ASSERT_EQ(bl_freeGeometryData(runtime,&data),BL_OK);
            ASSERT_EQ(bl_disposeRuntime(runtime),BL_OK);
            EXPECT_TRUE(state.live.empty());
            EXPECT_EQ(state.bytes,0u);
            if(reachedSuccess) break;
        }
        EXPECT_TRUE(reachedSuccess);
        Allocations state;
        bl_RuntimeOptions options{};
        options.allocator={&state,Allocations::Allocate,Allocations::Free};
        bl_Runtime* runtime{};
        ASSERT_EQ(bl_createRuntime(&options,&runtime),BL_OK);
        bl_GeometryData data{};
        for(size_t index=0;index<64;++index)
        {
            ASSERT_EQ(Create(family,runtime,&data),BL_OK);
            ASSERT_EQ(bl_freeGeometryData(runtime,&data),BL_OK);
        }
        const size_t warm=state.bytes;
        for(size_t index=0;index<10000;++index)
        {
            ASSERT_EQ(Create(family,runtime,&data),BL_OK);
            ASSERT_EQ(bl_freeGeometryData(runtime,&data),BL_OK);
        }
        EXPECT_EQ(state.bytes,warm);
        EXPECT_EQ(bl_disposeRuntime(runtime),BL_OK);
        EXPECT_TRUE(state.live.empty());
    }
}

TEST(LiteProceduralLifetime, AllocationReentryMisalignmentAndForeignStaleTokens)
{
    Allocations state;
    bl_RuntimeOptions options{};
    options.allocator={&state,Allocations::Allocate,Allocations::Free};
    ASSERT_EQ(bl_createRuntime(&options,&state.runtime),BL_OK);
    state.reenter=true;
    bl_GeometryData data{};
    ASSERT_EQ(Create(5,state.runtime,&data),BL_OK);
    EXPECT_EQ(state.nested,BL_BUSY);
    ASSERT_EQ(bl_freeGeometryData(state.runtime,&data),BL_OK);
    state.reenter=false;
    state.misalign=true;
    data.vertexCount=987;
    const auto original=data;
    EXPECT_EQ(Create(6,state.runtime,&data),BL_OUT_OF_MEMORY);
    EXPECT_EQ(std::memcmp(&data,&original,sizeof(data)),0);
    state.misalign=false;
    ASSERT_EQ(Create(6,state.runtime,&data),BL_OK);
    const auto stale=data;
    TestRuntime other;
    const auto live=data;
    EXPECT_EQ(bl_freeGeometryData(other.runtime,&data),BL_INVALID_HANDLE);
    EXPECT_EQ(std::memcmp(&data,&live,sizeof(data)),0);
    ASSERT_EQ(bl_freeGeometryData(state.runtime,&data),BL_OK);
    ASSERT_EQ(Create(2,state.runtime,&data),BL_OK);
    auto staleCopy=stale;
    EXPECT_EQ(bl_freeGeometryData(state.runtime,&staleCopy),BL_INVALID_HANDLE);
    EXPECT_EQ(std::memcmp(&staleCopy,&stale,sizeof(stale)),0);
    ASSERT_EQ(bl_freeGeometryData(state.runtime,&data),BL_OK);
    ASSERT_EQ(bl_disposeRuntime(state.runtime),BL_OK);
    EXPECT_TRUE(state.live.empty());
}

TEST(LiteProceduralLifetime, PointerOverflowAlignmentOutputAndCapValidation)
{
    TestRuntime runtime;
    bl_GeometryData sentinel{};
    sentinel.vertexCount=987;
    const auto original=sentinel;
    const bl_Vec3 path[]{{0,0,0},{1,0,0}};
    bl_TubeOptions tube{{path,2},{},{},static_cast<bl_GeometryCap>(4),{}};
    EXPECT_EQ(bl_createTubeData(runtime.runtime,&tube,&sentinel),BL_INVALID_ARGUMENT);
    tube.cap=BL_CAP_NONE;
    tube.path={path,std::numeric_limits<size_t>::max()};
    EXPECT_EQ(bl_createTubeData(runtime.runtime,&tube,&sentinel),BL_INVALID_ARGUMENT);
    tube.path={reinterpret_cast<const bl_Vec3*>(reinterpret_cast<const unsigned char*>(path)+1),2};
    EXPECT_EQ(bl_createTubeData(runtime.runtime,&tube,&sentinel),BL_INVALID_ARGUMENT);
    tube.path={nullptr,2};
    EXPECT_EQ(bl_createTubeData(runtime.runtime,&tube,&sentinel),BL_INVALID_ARGUMENT);
    const bl_Vec3Span row{path,2};
    bl_RibbonOptions ribbon{{&row,std::numeric_limits<size_t>::max()},false,false,{}};
    EXPECT_EQ(bl_createRibbonData(runtime.runtime,&ribbon,&sentinel),BL_INVALID_ARGUMENT);
    EXPECT_EQ(std::memcmp(&sentinel,&original,sizeof(sentinel)),0);
    EXPECT_EQ(bl_createRibbonData(runtime.runtime,nullptr,&sentinel),BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_createTubeData(runtime.runtime,nullptr,&sentinel),BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_createExtrudeShapeData(runtime.runtime,nullptr,&sentinel),BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_createCylinderData(runtime.runtime,nullptr,nullptr),BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_createPlaneData(runtime.runtime,nullptr,nullptr),BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_createPolyhedronData(runtime.runtime,nullptr,nullptr),BL_INVALID_ARGUMENT);
}
