#include "TestRuntime.h"
#include "LiteInternal.h"
#include <algorithm>
#include <random>
#include <vector>

namespace
{
    struct CountingAllocator
    {
        size_t calls{};
        size_t live{};
        int failAfter{-1};

        static void* Allocate(void* user, size_t bytes, size_t alignment)
        {
            auto& state = *static_cast<CountingAllocator*>(user);
            if (state.failAfter >= 0 && state.failAfter-- == 0)
            {
                return nullptr;
            }
            void* memory = TestRuntime::Allocate(nullptr, bytes, alignment);
            if (memory)
            {
                ++state.calls;
                state.live += bytes;
            }
            return memory;
        }

        static void Deallocate(void* user, void* memory, size_t bytes, size_t alignment)
        {
            static_cast<CountingAllocator*>(user)->live -= bytes;
            TestRuntime::Deallocate(nullptr, memory, bytes, alignment);
        }
    };

    std::vector<L_Draw> Reference(bl_Runtime* runtime, L_Scene* scene, const bl_Mat4& view)
    {
        std::vector<L_Draw> draws;
        for (size_t i = 0; i < scene->memberCount; ++i)
        {
            auto* node = scene->members[i];
            if (node->record.disposed || !node->visible || node->record.kind != L_MESH)
            {
                continue;
            }
            auto* mesh = reinterpret_cast<L_Mesh*>(node);
            if (!mesh->geometry.vertices || !mesh->geometry.indexCount ||
                !mesh->properties.material._id)
            {
                continue;
            }
            auto* material = reinterpret_cast<L_Material*>(
                l_peek(runtime, mesh->properties.material._id));
            EXPECT_EQ(l_world(runtime, node), BL_OK);
            const double depth = view.values[2] * node->world.values[12] +
                view.values[6] * node->world.values[13] +
                view.values[10] * node->world.values[14] + view.values[14];
            double order = mesh->properties.renderOrder.present
                ? mesh->properties.renderOrder.value : material->blending ? 200 : 100;
            size_t group = i;
            if (!material->blending)
            {
                for (size_t j = 0; j < scene->memberCount; ++j)
                {
                    auto* other = scene->members[j];
                    if (other->record.disposed || other->record.kind != L_MESH)
                    {
                        continue;
                    }
                    auto* member = reinterpret_cast<L_Mesh*>(other);
                    if (member->properties.material._id == material->record.id)
                    {
                        order = std::min(order, member->properties.renderOrder.present
                            ? member->properties.renderOrder.value : 100);
                        group = std::min(group, j);
                    }
                }
            }
            draws.push_back({mesh, material, order, depth, i, group, material->blending});
        }
        std::stable_sort(draws.begin(), draws.end(), [](const auto& a, const auto& b)
        {
            if (a.transparent != b.transparent) return !a.transparent;
            if (a.transparent && a.depth != b.depth) return a.depth > b.depth;
            if (a.order != b.order) return a.order < b.order;
            if (!a.transparent && a.group != b.group) return a.group < b.group;
            return a.sequence < b.sequence;
        });
        return draws;
    }

    void Compare(bl_Runtime* runtime, L_Scene* scene, const bl_Mat4& view)
    {
        const auto reference = Reference(runtime, scene, view);
        size_t count = SIZE_MAX;
        ASSERT_EQ(l_collectDraws(runtime, scene, &view, &count), BL_OK);
        ASSERT_EQ(count, reference.size());
        l_sortDraws(scene, count);
        auto* draws = static_cast<L_Draw*>(scene->drawScratch);
        for (size_t i = 0; i < count; ++i)
        {
            EXPECT_EQ(draws[i].mesh, reference[i].mesh);
            EXPECT_EQ(draws[i].material, reference[i].material);
            EXPECT_EQ(draws[i].order, reference[i].order);
            EXPECT_EQ(draws[i].depth, reference[i].depth);
            EXPECT_EQ(draws[i].sequence, reference[i].sequence);
            EXPECT_EQ(draws[i].group, reference[i].group);
        }
    }
}

TEST(LiteRenderOrder, CurrentFrameAggregationMatchesCompleteScanAfterMutations)
{
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    constexpr size_t MaterialCount = 8;
    L_Material* materials[MaterialCount]{};
    for (size_t i = 0; i < MaterialCount; ++i)
    {
        L_Record* record{};
        ASSERT_EQ(l_record(runtime.runtime, sizeof(L_Material), L_MATERIAL, 20, nullptr,
            &record), BL_OK);
        materials[i] = reinterpret_cast<L_Material*>(record);
        materials[i]->blending = i >= 5;
    }
    std::mt19937 random{1337};
    L_Mesh meshes[90]{};
    L_Node* members[100]{};
    bool owners[100]{};
    L_Scene scene{};
    scene.members = members;
    scene.memberOwners = owners;
    bl_Mat4 view{};
    l_identity(&view);
    const double orders[]{-10, 0, 50, 100, 200, 1000};
    for (size_t iteration = 0; iteration < 250; ++iteration)
    {
        scene.memberCount = 1 + random() % 90;
        for (size_t i = 0; i < scene.memberCount; ++i)
        {
            auto& mesh = meshes[i];
            mesh = {};
            mesh.node.record.kind = L_MESH;
            l_initNode(&mesh.node);
            mesh.node.record.disposed = random() % 10 == 0;
            mesh.node.visible = random() % 5 != 0;
            mesh.node.position.z = static_cast<double>(random() % 7);
            mesh.geometry.vertices = random() % 10 == 0 ? 0 : 3;
            mesh.geometry.indexCount = 3;
            mesh.properties.material = {runtime.runtime, materials[random() % MaterialCount]->record.id};
            mesh.properties.renderOrder = {random() % 3 != 0, orders[random() % 6]};
            members[i] = &mesh.node;
            owners[i] = true;
        }
        members[scene.memberCount] = members[0];
        owners[scene.memberCount++] = false;
        Compare(runtime.runtime, &scene, view);
        // No cross-frame key: hidden/empty members, swaps and removals must all be visited again.
        meshes[0].properties.renderOrder = {true, -1000};
        meshes[0].node.visible = false;
        meshes[1].properties.material = {runtime.runtime, materials[0]->record.id};
        Compare(runtime.runtime, &scene, view);
        --scene.memberCount;
        Compare(runtime.runtime, &scene, view);
    }
    l_free(runtime.runtime, scene.drawScratch);
    l_free(runtime.runtime, scene.groupScratch);
}

TEST(LiteRenderOrder, ScratchGrowthIsTransactionalAndWarmFramesAllocateNothing)
{
    CountingAllocator allocator;
    bl_RuntimeOptions options{};
    options.allocator = {&allocator, CountingAllocator::Allocate, CountingAllocator::Deallocate};
    bl_Runtime* runtime{};
    ASSERT_EQ(bl_createRuntime(&options, &runtime), BL_OK);
    L_Node nodes[40]{};
    L_Node* members[40]{};
    for (size_t i = 0; i < 40; ++i)
    {
        members[i] = nodes + i;
    }
    L_Scene scene{};
    scene.members = members;
    scene.memberCount = 1;
    bl_Mat4 view{};
    l_identity(&view);
    size_t count{};
    ASSERT_EQ(l_collectDraws(runtime, &scene, &view, &count), BL_OK);
    auto* draws = scene.drawScratch;
    auto* groups = scene.groupScratch;
    const auto capacity = scene.drawCapacity;
    const auto bytes = allocator.live;
    scene.memberCount = 40;
    for (int failure = 0; failure < 2; ++failure)
    {
        allocator.failAfter = failure;
        count = 123;
        EXPECT_EQ(l_collectDraws(runtime, &scene, &view, &count), BL_OUT_OF_MEMORY);
        EXPECT_EQ(count, 123u);
        EXPECT_EQ(scene.drawScratch, draws);
        EXPECT_EQ(scene.groupScratch, groups);
        EXPECT_EQ(scene.drawCapacity, capacity);
        EXPECT_EQ(allocator.live, bytes);
    }
    allocator.failAfter = -1;
    ASSERT_EQ(l_collectDraws(runtime, &scene, &view, &count), BL_OK);
    const auto calls = allocator.calls;
    for (size_t frame = 0; frame < 10000; ++frame)
    {
        scene.memberCount = 1 + frame % 40;
        ASSERT_EQ(l_collectDraws(runtime, &scene, &view, &count), BL_OK);
    }
    EXPECT_EQ(allocator.calls, calls);
    l_free(runtime, scene.drawScratch);
    l_free(runtime, scene.groupScratch);
    ASSERT_EQ(bl_disposeRuntime(runtime), BL_OK);
    EXPECT_EQ(allocator.live, 0u);
}
