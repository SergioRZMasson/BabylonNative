#include "TestRuntime.h"

namespace
{
    bl_String String(const char* value, size_t count)
    {
        return {value, count};
    }
}

TEST(LiteNodes, MutablePositionAndSeparateParentChildrenOperationsPreserveSourceSemantics)
{
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    bl_TransformNode parent{}, child{};
    ASSERT_EQ(bl_createTransformNode(runtime.runtime, String("parent", 6), nullptr, &parent), BL_OK);
    ASSERT_EQ(bl_createTransformNode(runtime.runtime, String("child", 5), nullptr, &child), BL_OK);
    ASSERT_EQ(bl_setNodePosition(parent, {10, 20, 30}), BL_OK);
    ASSERT_EQ(bl_setNodePosition(child, {1, 2, 3}), BL_OK);
    ASSERT_EQ(bl_setNodeParent(child, parent), BL_OK);
    bl_NodeChildren children{};
    ASSERT_EQ(bl_getNodeChildren(parent, &children), BL_OK);
    EXPECT_EQ(children.count, 0u);
    ASSERT_EQ(bl_appendNodeChild(parent, child), BL_OK);
    ASSERT_EQ(bl_getNodeChildren(parent, &children), BL_OK);
    EXPECT_EQ(children.count, 1u);
    bl_Vec3 local{};
    ASSERT_EQ(bl_getNodePosition(child, &local), BL_OK);
    EXPECT_EQ(local.x, 1);
    EXPECT_EQ(local.y, 2);
    EXPECT_EQ(local.z, 3);
    bl_Mat4 world{};
    uint64_t version{};
    ASSERT_EQ(bl_getNodeWorldMatrix(child, &world, &version), BL_OK);
    EXPECT_NEAR(world.values[12], 11, 1e-6);
    EXPECT_NEAR(world.values[13], 22, 1e-6);
    EXPECT_NEAR(world.values[14], 33, 1e-6);
    const uint64_t before = version;
    ASSERT_EQ(bl_setNodePosition(parent, {11, 20, 30}), BL_OK);
    ASSERT_EQ(bl_getNodeWorldMatrix(child, &world, &version), BL_OK);
    EXPECT_NEAR(world.values[12], 12, 1e-6);
    EXPECT_NE(version, before);
    EXPECT_EQ(bl_setNodeParent(parent, child), BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_removeNodeChild(parent, child), BL_OK);
    EXPECT_EQ(bl_setNodeParent(child, {}), BL_OK);
    EXPECT_EQ(bl_disposeNode(child), BL_OK);
    EXPECT_EQ(bl_disposeNode(parent), BL_OK);
}

TEST(LiteNodes, ConsecutiveEulerWritesAndZeroScaleRemainRepresentable)
{
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    bl_TransformNode node{};
    ASSERT_EQ(bl_createTransformNode(runtime.runtime, {}, nullptr, &node), BL_OK);
    ASSERT_EQ(bl_setNodeRotation(node, {0.5, 1.5707963267948966, 0.75}), BL_OK);
    bl_Vec3 rotation{};
    ASSERT_EQ(bl_getNodeRotation(node, &rotation), BL_OK);
    EXPECT_DOUBLE_EQ(rotation.x, 0.5);
    EXPECT_DOUBLE_EQ(rotation.z, 0.75);
    rotation.x = 0.6;
    ASSERT_EQ(bl_setNodeRotation(node, rotation), BL_OK);
    ASSERT_EQ(bl_getNodeRotation(node, &rotation), BL_OK);
    EXPECT_DOUBLE_EQ(rotation.x, 0.6);
    EXPECT_DOUBLE_EQ(rotation.y, 1.5707963267948966);
    EXPECT_DOUBLE_EQ(rotation.z, 0.75);
    ASSERT_EQ(bl_setNodeScaling(node, {0, 0, 0}), BL_OK);
    bl_Vec3 scaling{};
    ASSERT_EQ(bl_getNodeScaling(node, &scaling), BL_OK);
    EXPECT_EQ(scaling.x, 0);
    EXPECT_EQ(scaling.y, 0);
    EXPECT_EQ(scaling.z, 0);
    EXPECT_EQ(bl_disposeNode(node), BL_OK);
}
