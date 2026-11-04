TEST_F(LiteNativeGraphics, M1CloneRejectsSharedWritesButResizeDetachesAndLastOwnerSurvives)
{
    CreateColoredBox();
    bl_SceneNode cloned{};
    ASSERT_EQ(bl_cloneTransformNode(meshNode, &cloned), BL_OK);
    bl_Mesh copy{};
    ASSERT_EQ(bl_sceneNodeAsMesh(cloned, &copy), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, cloned), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(Center()[0], 255);
    bl_GeometryData data{};
    ASSERT_EQ(bl_createBoxData(runtime->runtime, nullptr, &data), BL_OK);
    std::vector<float> moved(data.positions, data.positions + data.vertexCount * 3);
    for (size_t i = 0; i < moved.size(); i += 3)
        moved[i] += 100;
    ASSERT_EQ(bl_updateMeshPositions(engine, mesh, {moved.data(), moved.size()}, 0, nullptr, 0), BL_UNSUPPORTED);
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(Center()[0], 255);
    bl_MeshGeometry geometry{{moved.data(), moved.size()}, {data.normals, data.vertexCount * 3},
        {data.indices, data.indexCount}, {data.uvs, data.vertexCount * 2}, {}, {}, {}};
    EXPECT_EQ(bl_updateMeshGeometry(engine, mesh, &geometry), BL_UNSUPPORTED);
    bl_GeometryCapacityResult capacity{true, 123, 456};
    EXPECT_EQ(bl_updateMeshGeometryCapacity(engine, mesh, &geometry, nullptr, nullptr, &capacity), BL_UNSUPPORTED);
    EXPECT_EQ(capacity.vertexCapacity, 123u);
    ASSERT_EQ(bl_resizeMeshGeometry(engine, mesh, &geometry), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(Center()[0], 255);
    ASSERT_EQ(bl_removeFromScene(scene, meshNode), BL_OK);
    ASSERT_EQ(bl_updateMeshPositions(engine, copy, {moved.data(), moved.size()}, 0, nullptr, 0), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_LT(Center()[0], 50);
    ASSERT_EQ(bl_updateMeshPositions(engine, copy, {data.positions, data.vertexCount * 3}, 0, nullptr, 0), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(Center()[0], 255);
    EXPECT_EQ(bl_freeGeometryData(runtime->runtime, &data), BL_OK);
}

TEST_F(LiteNativeGraphics, M1CloneRetainsSharedF64ThinSlabAndIndependentLastOwner)
{
    bl_Mesh prototype{};
    ASSERT_EQ(bl_createBox(engine, nullptr, &prototype), BL_OK);
    bl_Mat4 identity{};
    ASSERT_EQ(bl_createIdentityMat4(&identity), BL_OK);
    ASSERT_EQ(bl_setThinInstancesF64(prototype, {identity.values, 16}, 1), BL_OK);
    bl_SceneNode node{};
    ASSERT_EQ(bl_meshNode(prototype, &node), BL_OK);
    bl_SceneNode cloned{};
    ASSERT_EQ(bl_cloneTransformNode(node, &cloned), BL_OK);
    bl_Mesh copy{};
    ASSERT_EQ(bl_sceneNodeAsMesh(cloned, &copy), BL_OK);
    bl_Mat4 moved{};
    ASSERT_EQ(bl_createTranslationMat4(.1234567890123, 1, 2, true, &moved), BL_OK);
    ASSERT_EQ(bl_setThinInstanceMatrix(copy, 0, &moved), BL_OK);
    bl_ThinInstanceView2 first{};
    bl_ThinInstanceView2 second{};
    ASSERT_EQ(bl_getThinInstances2(prototype, &first), BL_OK);
    ASSERT_EQ(bl_getThinInstances2(copy, &second), BL_OK);
    EXPECT_EQ(first.matrices64.data, second.matrices64.data);
    EXPECT_DOUBLE_EQ(first.matrices64.data[12], moved.values[12]);
    EXPECT_EQ(first.base.version, second.base.version);
    ASSERT_EQ(bl_disposeNode(cloned), BL_OK);
    ASSERT_EQ(bl_getThinInstances2(prototype, &first), BL_OK);
    EXPECT_DOUBLE_EQ(first.matrices64.data[12], moved.values[12]);
    EXPECT_EQ(bl_disposeNode(node), BL_OK);
}
