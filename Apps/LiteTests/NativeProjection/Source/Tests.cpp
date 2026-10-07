#include "Application.h"
#include "C99Projection.h"
#include "../../NativeTests/Source/TestRuntime.h"

TEST(LiteNativeProjection, TranspiledGeometryUsesC99FactoryWithoutGeneratedEngine)
{
    TestRuntime native;
    ASSERT_EQ(native.status, BL_OK);
    LiteProjection::BindRuntime(native.runtime);
    EXPECT_EQ(LiteProjectedApplication::geometryCycles(10000, 2.5), 10000);
    EXPECT_EQ(LiteProjectedApplication::geometryCycles(1000, 0), 1000);
    EXPECT_EQ(LiteProjection::RetainedApplicationTokens(), 0u);
    LiteProjection::BindRuntime(nullptr);
}

TEST(LiteNativeProjection, TranspiledHierarchyUsesC99StateWithoutGeneratedEngine)
{
    TestRuntime native;
    ASSERT_EQ(native.status, BL_OK);
    LiteProjection::BindRuntime(native.runtime);
    EXPECT_EQ(LiteProjectedApplication::hierarchyCycles(10000, 10.25), 10000);
    EXPECT_EQ(LiteProjection::RetainedApplicationTokens(), 0u);
    LiteProjection::BindRuntime(nullptr);
}
