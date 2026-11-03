#include "TestRuntime.h"
#include "AssetInternal.h"
#include "LightInternal.h"
#include <array>

namespace
{
    template<typename T>
    T* Record(TestRuntime& runtime, unsigned kind)
    {
        L_Record* record{};
        EXPECT_EQ(l_record(runtime.runtime, sizeof(T), kind, 10, nullptr, &record), BL_OK);
        return reinterpret_cast<T*>(record);
    }
}

TEST(LiteInspection, SceneListsPreserveDuplicatesOrderAndHiddenEmptyMeshes)
{
    TestRuntime r;
    auto* scene = Record<L_Scene>(r, L_SCENE);
    auto* first = Record<L_Mesh>(r, L_MESH);
    auto* second = Record<L_Mesh>(r, L_MESH);
    auto* light = Record<L_Light>(r, L_LIGHT);
    first->node.visible = false;
    L_Node* members[]{&first->node, &light->node, &second->node, &first->node, &light->node};
    scene->members = members;
    scene->memberCount = 5;
    const bl_SceneContext h{r.runtime, scene->record.id};
    size_t count = 99;
    ASSERT_EQ(bl_getSceneMeshes(h, nullptr, 0, &count), BL_OK);
    EXPECT_EQ(count, 3u);
    std::array<bl_Mesh, 4> meshes{};
    meshes[0]._id = 123;
    count = 99;
    EXPECT_EQ(bl_getSceneMeshes(h, meshes.data(), 2, &count), BL_INVALID_ARGUMENT);
    EXPECT_EQ(count, 99u);
    EXPECT_EQ(meshes[0]._id, 123u);
    ASSERT_EQ(bl_getSceneMeshes(h, meshes.data(), meshes.size(), &count), BL_OK);
    EXPECT_EQ(meshes[0]._id, first->node.record.id);
    EXPECT_EQ(meshes[1]._id, second->node.record.id);
    EXPECT_EQ(meshes[2]._id, first->node.record.id);
    bl_Light lights[2]{};
    ASSERT_EQ(bl_getSceneLights(h, lights, 2, &count), BL_OK);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(lights[0]._id, light->node.record.id);
    EXPECT_EQ(lights[1]._id, light->node.record.id);
    lights[0]._id = 123;
    count = 99;
    EXPECT_EQ(bl_getSceneLights(h, lights, 1, &count), BL_INVALID_ARGUMENT);
    EXPECT_EQ(lights[0]._id, 123u);
    EXPECT_EQ(count, 99u);
    light->node.record.disposed = true;
    ASSERT_EQ(bl_getSceneLights(h, nullptr, 0, &count), BL_OK);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(bl_getSceneMeshes(h, nullptr, 1, &count), BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_getSceneMeshes(h, meshes.data(), 4, nullptr), BL_INVALID_ARGUMENT);
    alignas(bl_Mesh) uint8_t unaligned[sizeof(bl_Mesh) + 1]{};
    EXPECT_EQ(bl_getSceneMeshes(h, reinterpret_cast<bl_Mesh*>(unaligned + 1), 1, &count),
        BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_getSceneMeshes(h, meshes.data(), SIZE_MAX, &count), BL_INVALID_ARGUMENT);
    scene->record.disposed = true;
    count = 99;
    EXPECT_EQ(bl_getSceneMeshes(h, nullptr, 0, &count), BL_DISPOSED);
    EXPECT_EQ(count, 99u);
}

TEST(LiteInspection, ContainerDepthFirstDedupLocalBoundsAndTransactionalFailure)
{
    TestRuntime r;
    auto* asset = Record<L_Asset>(r, L_ASSET);
    auto* root = Record<L_Node>(r, L_NODE);
    auto* branch = Record<L_Node>(r, L_NODE);
    auto* first = Record<L_Mesh>(r, L_MESH);
    auto* second = Record<L_Mesh>(r, L_MESH);
    const bl_SceneNode firstNode{r.runtime, first->node.record.id};
    const bl_SceneNode secondNode{r.runtime, second->node.record.id};
    const bl_SceneNode branchNode{r.runtime, branch->record.id};
    bl_SceneNode rootChildren[]{branchNode, firstNode, branchNode};
    bl_SceneNode branchChildren[]{secondNode, firstNode, {r.runtime, root->record.id}};
    root->children = rootChildren;
    root->childCount = 3;
    branch->children = branchChildren;
    branch->childCount = 3;
    asset->root = {r.runtime, root->record.id};
    const bl_AssetContainer h{r.runtime, asset->record.id};
    bl_Mesh result[2]{};
    size_t count = 88;
    ASSERT_EQ(bl_getContainerMeshes(h, result, 2, &count), BL_OK);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(result[0]._id, secondNode._id);
    EXPECT_EQ(result[1]._id, firstNode._id);
    result[0]._id = 42;
    count = 88;
    EXPECT_EQ(bl_getContainerMeshes(h, result, 1, &count), BL_INVALID_ARGUMENT);
    EXPECT_EQ(result[0]._id, 42u);
    EXPECT_EQ(count, 88u);
    const auto allocate = r.runtime->allocator.allocate;
    r.runtime->allocator.allocate = [](void*, size_t, size_t) -> void* { return nullptr; };
    EXPECT_EQ(bl_getContainerMeshes(h, result, 2, &count), BL_OUT_OF_MEMORY);
    EXPECT_EQ(result[0]._id, 42u);
    EXPECT_EQ(count, 88u);
    r.runtime->allocator.allocate = allocate;
    first->geometry.minimum = {-1, -2, -3};
    first->geometry.maximum = {4, 5, 6};
    first->node.position = {100, 200, 300};
    bl_Vec3 minimum{};
    bl_Vec3 maximum{};
    ASSERT_EQ(bl_getMeshBounds({r.runtime, firstNode._id}, &minimum, &maximum), BL_OK);
    EXPECT_DOUBLE_EQ(minimum.x, -1);
    EXPECT_DOUBLE_EQ(maximum.z, 6);
    minimum.x = 99;
    EXPECT_EQ(bl_getMeshBounds({r.runtime, firstNode._id}, &minimum, nullptr), BL_INVALID_ARGUMENT);
    EXPECT_DOUBLE_EQ(minimum.x, 99);
    EXPECT_EQ(bl_getMeshBounds({r.runtime, root->record.id}, &minimum, &maximum), BL_INVALID_HANDLE);
    first->node.record.disposed = true;
    EXPECT_EQ(bl_getMeshBounds({r.runtime, firstNode._id}, &minimum, &maximum), BL_DISPOSED);
    auto* reused = Record<L_Mesh>(r, L_MESH);
    EXPECT_NE(reused->node.record.id, firstNode._id);
    EXPECT_EQ(bl_getMeshBounds({r.runtime, firstNode._id}, &minimum, &maximum), BL_DISPOSED);
}

TEST(LiteInspection, MaterialNameBorrowsNativeStorageAndRejectsDisposedAndWrongKinds)
{
    TestRuntime r;
    bl_StandardMaterial standard{};
    ASSERT_EQ(bl_createStandardMaterial(r.runtime, &standard), BL_OK);
    bl_Material material{};
    ASSERT_EQ(bl_standardMaterialAsMaterial(standard, &material), BL_OK);
    auto* record = reinterpret_cast<L_Material*>(l_peek(r.runtime, material._id));
    bl_String name{};
    ASSERT_EQ(bl_getMaterialName(material, &name), BL_OK);
    EXPECT_EQ(name.data, record->name.data);
    EXPECT_EQ(name.length, record->name.length);
    EXPECT_EQ(bl_getMaterialName(material, nullptr), BL_INVALID_ARGUMENT);
    ASSERT_EQ(bl_disposeStandardMaterial(standard), BL_OK);
    name = {"sentinel", 8};
    EXPECT_EQ(bl_getMaterialName(material, &name), BL_DISPOSED);
    EXPECT_EQ(name.length, 8u);
    bl_TransformNode node{};
    ASSERT_EQ(bl_createTransformNode(r.runtime, {}, nullptr, &node), BL_OK);
    EXPECT_EQ(bl_getMaterialName({r.runtime, node._id}, &name), BL_INVALID_HANDLE);
}
