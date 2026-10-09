#include "TestRuntime.h"
#include "DirectionalLightInternal.h"
#include "HemisphericLightInternal.h"
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
    std::vector<T> Fixture(const char* name)
    {
        std::ifstream stream(std::filesystem::path(BL_ORACLE_DIRECTORY) / name,
            std::ios::binary | std::ios::ate);
        if (!stream)
        {
            ADD_FAILURE() << "Missing original Directional fixture " << name;
            return {};
        }
        const auto bytes = stream.tellg();
        std::vector<T> result(static_cast<size_t>(bytes) / sizeof(T));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(result.data()), bytes);
        return result;
    }

    bl_SceneNode Node(bl_DirectionalLight light)
    {
        bl_Light family{};
        bl_SceneNode node{};
        EXPECT_EQ(bl_directionalLightAsLight(light, &family), BL_OK);
        EXPECT_EQ(bl_lightNode(family, &node), BL_OK);
        return node;
    }

    void Golden(TestRuntime& host, bl_DirectionalLight light, const char* name)
    {
        L_DirectionalLight* record{};
        ASSERT_EQ(l_directionalLight(light, &record), BL_OK);
        float data[16]{};
        ASSERT_EQ(l_writeDirectionalLight(host.runtime, record, data), BL_OK);
        const auto expected = Fixture<float>((std::string("light-") + name + ".bin").c_str());
        ASSERT_EQ(expected.size(), 16u);
        EXPECT_EQ(std::memcmp(data, expected.data(), sizeof(data)), 0) << name;
        EXPECT_TRUE(std::isinf(data[7]) && data[7] > 0);
    }

    struct Allocations
    {
        size_t calls{};
        size_t fail{};
        size_t bytes{};
        std::unordered_map<void*, size_t> live;
        static void* Allocate(void* user, size_t bytes, size_t alignment)
        {
            auto& state = *static_cast<Allocations*>(user);
            if (++state.calls == state.fail) return nullptr;
            void* memory = TestRuntime::Allocate(nullptr, bytes, alignment);
            if (memory)
            {
                state.live.emplace(memory, bytes);
                state.bytes += bytes;
            }
            return memory;
        }
        static void Free(void* user, void* memory, size_t bytes, size_t alignment)
        {
            auto& state = *static_cast<Allocations*>(user);
            EXPECT_EQ(state.live.erase(memory), 1u);
            state.bytes -= bytes;
            TestRuntime::Deallocate(nullptr, memory, bytes, alignment);
        }
    };
}

TEST(LiteDirectional, DefaultsRequiredOptionsTransactionalPropertiesAndHelper134)
{
    TestRuntime host;
    bl_DirectionalLight light{host.runtime, 99};
    EXPECT_EQ(bl_createDirectionalLight(host.runtime, nullptr, &light), BL_INVALID_ARGUMENT);
    EXPECT_EQ(light._id, 99u);
    bl_DirectionalLightOptions options{{0, -1, 0}, {}};
    EXPECT_EQ(bl_createDirectionalLight(host.runtime, &options, nullptr), BL_INVALID_ARGUMENT);
    options.intensity = {true, std::numeric_limits<double>::infinity()};
    EXPECT_EQ(bl_createDirectionalLight(host.runtime, &options, &light), BL_INVALID_ARGUMENT);
    EXPECT_EQ(light._id, 99u);
    options.intensity.present = false;
    ASSERT_EQ(bl_createDirectionalLight(host.runtime, &options, &light), BL_OK);
    bl_DirectionalLightProperties p{};
    ASSERT_EQ(bl_getDirectionalLightProperties(light, &p), BL_OK);
    EXPECT_EQ(p.intensity, 1);
    EXPECT_EQ(p.diffuse.x, 1);
    EXPECT_EQ(p.specular.z, 1);
    L_DirectionalLight* native{};
    ASSERT_EQ(l_directionalLight(light, &native), BL_OK);
    EXPECT_EQ(native->light.kind, L_LIGHT_DIRECTIONAL);
    EXPECT_EQ(native->light.node.scale.x, 1);
    EXPECT_TRUE(native->light.node.visible);
    const auto saved = p;
    p.diffuse.x = std::numeric_limits<double>::infinity();
    EXPECT_EQ(bl_setDirectionalLightProperties(light, &p), BL_INVALID_ARGUMENT);
    ASSERT_EQ(bl_getDirectionalLightProperties(light, &p), BL_OK);
    EXPECT_EQ(std::memcmp(&saved, &p, sizeof(p)), 0);
    EXPECT_EQ(native->light.dataVersion, 0u);
    p.intensity = -3;
    p.diffuse = {-1, .25, 2};
    p.specular = {4, -5, 6};
    ASSERT_EQ(bl_setDirectionalLightProperties(light, &p), BL_OK);
    EXPECT_EQ(native->light.dataVersion, 0u);
    bl_Light family{};
    ASSERT_EQ(bl_directionalLightAsLight(light, &family), BL_OK);
    ASSERT_EQ(bl_setLightIntensity(family, -3), BL_OK);
    EXPECT_EQ(native->light.dataVersion, 0u);
    ASSERT_EQ(bl_setLightIntensity(family, -2), BL_OK);
    EXPECT_EQ(native->light.dataVersion, 1u);
    EXPECT_EQ(bl_setLightIntensity(family, std::numeric_limits<double>::quiet_NaN()), BL_INVALID_ARGUMENT);
    bl_Error error{};
    ASSERT_EQ(bl_getLastError(host.runtime, &error), BL_OK);
    EXPECT_EQ(error.sourceErrorCode, 134u);
    ASSERT_EQ(bl_markLightUboDirty(family), BL_OK);
    EXPECT_EQ(native->light.dataVersion, 2u);
    ASSERT_EQ(bl_setDirectionalLightProperties(light, &p), BL_OK);
    EXPECT_EQ(native->light.dataVersion, 2u);
    p.direction = {};
    ASSERT_EQ(bl_setDirectionalLightProperties(light, &p), BL_OK);
    EXPECT_EQ(native->light.dataVersion, 3u);
}

TEST(LiteDirectional, ConcreteCrossFamilyForgeryGuardsAndSentinels)
{
    TestRuntime host;
    TestRuntime other;
    bl_DirectionalLight directional{};
    const bl_DirectionalLightOptions options{{0, -1, 0}, {}};
    ASSERT_EQ(bl_createDirectionalLight(host.runtime, &options, &directional), BL_OK);
    bl_HemisphericLight hemi{};
    ASSERT_EQ(bl_createHemisphericLight(host.runtime, nullptr, &hemi), BL_OK);
    bl_DirectionalLightProperties dp{};
    std::memset(&dp, 0x5a, sizeof(dp));
    const auto ds = dp;
    EXPECT_EQ(bl_getDirectionalLightProperties({host.runtime, hemi._id}, &dp), BL_INVALID_HANDLE);
    EXPECT_EQ(std::memcmp(&dp, &ds, sizeof(dp)), 0);
    EXPECT_EQ(bl_setDirectionalLightProperties({host.runtime, hemi._id}, &dp), BL_INVALID_HANDLE);
    bl_HemisphericLightProperties hp{};
    std::memset(&hp, 0x6b, sizeof(hp));
    const auto hs = hp;
    EXPECT_EQ(bl_getHemisphericLightProperties({host.runtime, directional._id}, &hp), BL_INVALID_HANDLE);
    EXPECT_EQ(std::memcmp(&hp, &hs, sizeof(hp)), 0);
    EXPECT_EQ(bl_setHemisphericLightProperties({host.runtime, directional._id}, &hp), BL_INVALID_HANDLE);
    bl_Light family{other.runtime, 123};
    EXPECT_EQ(bl_hemisphericLightAsLight({host.runtime, directional._id}, &family), BL_INVALID_HANDLE);
    EXPECT_EQ(family._id, 123u);
    EXPECT_EQ(bl_directionalLightAsLight({host.runtime, hemi._id}, &family), BL_INVALID_HANDLE);
    EXPECT_EQ(family._runtime, other.runtime);
    bl_DirectionalLight output{other.runtime, 456};
    EXPECT_EQ(bl_lightAsDirectionalLight({host.runtime, hemi._id}, &output), BL_INVALID_HANDLE);
    EXPECT_EQ(output._id, 456u);
    EXPECT_EQ(bl_lightAsDirectionalLight({}, &output), BL_INVALID_HANDLE);
    EXPECT_EQ(output._id, 456u);
    EXPECT_EQ(bl_directionalLightAsLight(directional, nullptr), BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_lightAsDirectionalLight({host.runtime, directional._id}, nullptr), BL_INVALID_ARGUMENT);
    bl_Status wrongThread{};
    std::thread worker([&] { wrongThread = bl_getDirectionalLightProperties(directional, &dp); });
    worker.join();
    EXPECT_EQ(wrongThread, BL_WRONG_THREAD);
    EXPECT_EQ(std::memcmp(&dp, &ds, sizeof(dp)), 0);
    auto* native = reinterpret_cast<L_DirectionalLight*>(l_peek(host.runtime, directional._id));
    native->light.kind = 99;
    bl_SceneNode node{other.runtime, 789};
    EXPECT_EQ(bl_lightNode({host.runtime, directional._id}, &node), BL_INVALID_HANDLE);
    EXPECT_EQ(node._id, 789u);
    EXPECT_EQ(bl_setLightIntensity({host.runtime, directional._id}, 4), BL_INVALID_HANDLE);
    EXPECT_EQ(bl_markLightUboDirty({host.runtime, directional._id}), BL_INVALID_HANDLE);
    bl_Vec3 position{99, 88, 77};
    EXPECT_EQ(bl_getNodePosition({host.runtime, directional._id}, &position), BL_INVALID_HANDLE);
    EXPECT_EQ(position.x, 99);
    native->light.kind = L_LIGHT_DIRECTIONAL;
    const auto stale = directional;
    ASSERT_EQ(bl_disposeNode(Node(directional)), BL_OK);
    for (unsigned i = 0; i < 64; ++i)
    {
        bl_HemisphericLight fresh{};
        ASSERT_EQ(bl_createHemisphericLight(host.runtime, nullptr, &fresh), BL_OK);
        EXPECT_EQ(bl_getDirectionalLightProperties(stale, &dp), BL_DISPOSED);
        bl_Light hf{};
        bl_SceneNode hn{};
        ASSERT_EQ(bl_hemisphericLightAsLight(fresh, &hf), BL_OK);
        ASSERT_EQ(bl_lightNode(hf, &hn), BL_OK);
        ASSERT_EQ(bl_disposeNode(hn), BL_OK);
    }
}

TEST(LiteDirectional, TwelveOriginalLightWriterByteGoldens)
{
    TestRuntime host;
    const bl_DirectionalLightOptions options{{0, -1, 0}, {}};
    bl_DirectionalLight light{};
    ASSERT_EQ(bl_createDirectionalLight(host.runtime, &options, &light), BL_OK);
    Golden(host, light, "defaults");
    bl_DirectionalLightProperties p{};
    ASSERT_EQ(bl_getDirectionalLightProperties(light, &p), BL_OK);
    p.diffuse = {1, 0, 0};
    p.specular = {0, 1, 0};
    ASSERT_EQ(bl_setDirectionalLightProperties(light, &p), BL_OK);
    Golden(host, light, "scene2");
    p.intensity = .5;
    ASSERT_EQ(bl_setDirectionalLightProperties(light, &p), BL_OK);
    Golden(host, light, "raw-intensity");
    bl_Light family{};
    ASSERT_EQ(bl_directionalLightAsLight(light, &family), BL_OK);
    ASSERT_EQ(bl_setLightIntensity(family, 2), BL_OK);
    Golden(host, light, "helper-intensity");
    ASSERT_EQ(bl_setLightIntensity(family, -1.5), BL_OK);
    Golden(host, light, "negative-intensity");
    ASSERT_EQ(bl_getDirectionalLightProperties(light, &p), BL_OK);
    p.diffuse = {.25, .5, .75};
    ASSERT_EQ(bl_setDirectionalLightProperties(light, &p), BL_OK);
    ASSERT_EQ(bl_markLightUboDirty(family), BL_OK);
    Golden(host, light, "helper-color");
    p.direction = {2, -3, 4};
    ASSERT_EQ(bl_setDirectionalLightProperties(light, &p), BL_OK);
    Golden(host, light, "nonunit");
    p.direction = {};
    ASSERT_EQ(bl_setDirectionalLightProperties(light, &p), BL_OK);
    Golden(host, light, "zero");
    p.direction = {2, -3, 4};
    ASSERT_EQ(bl_setDirectionalLightProperties(light, &p), BL_OK);
    ASSERT_EQ(bl_setNodePosition(Node(light), {7, 8, 9}), BL_OK);
    Golden(host, light, "translated");
    bl_TransformNode parent{};
    ASSERT_EQ(bl_createTransformNode(host.runtime, {}, nullptr, &parent), BL_OK);
    ASSERT_EQ(bl_setNodeRotation(parent, {.3, .7, -.2}), BL_OK);
    ASSERT_EQ(bl_setNodeScaling(parent, {2, .5, 3}), BL_OK);
    ASSERT_EQ(bl_setNodePosition(parent, {11, 12, 13}), BL_OK);
    ASSERT_EQ(bl_setNodeParent(Node(light), parent), BL_OK);
    Golden(host, light, "parented-nonuniform");
    ASSERT_EQ(bl_setNodeScaling(parent, {}), BL_OK);
    Golden(host, light, "zero-parent-scale");
    bl_HemisphericLight hemi{};
    ASSERT_EQ(bl_createHemisphericLight(host.runtime, nullptr, &hemi), BL_OK);
    L_HemisphericLight* h{};
    ASSERT_EQ(l_hemisphericLight(hemi, &h), BL_OK);
    float data[16]{};
    ASSERT_EQ(l_writeHemisphericLight(host.runtime, h, data), BL_OK);
    const auto expected = Fixture<float>("light-hemispheric-regression.bin");
    ASSERT_EQ(expected.size(), 16u);
    EXPECT_EQ(std::memcmp(data, expected.data(), sizeof(data)), 0);
}

TEST(LiteDirectional, TwoHundredOriginalF32AndHpmParentDirectionOracles)
{
    const auto inputs = Fixture<double>("directional-random-inputs.f64.bin");
    const auto data = Fixture<float>("directional-random-data.f32.bin");
    const auto worlds = Fixture<double>("directional-random-world.f64.bin");
    ASSERT_EQ(inputs.size(), 200u * 16);
    ASSERT_EQ(data.size(), 200u * 16);
    ASSERT_EQ(worlds.size(), 200u * 16);
    TestRuntime host;
    for (unsigned i = 0; i < 200; ++i)
    {
        SCOPED_TRACE(i);
        const double* p = inputs.data() + i * 16;
        bl_TransformNode parent{};
        ASSERT_EQ(bl_createTransformNode(host.runtime, {}, nullptr, &parent), BL_OK);
        bl_DirectionalLight light{};
        const bl_DirectionalLightOptions options{{p[1], p[2], p[3]}, {}};
        ASSERT_EQ(bl_createDirectionalLight(host.runtime, &options, &light), BL_OK);
        L_DirectionalLight* native{};
        ASSERT_EQ(l_directionalLight(light, &native), BL_OK);
        native->light.node.highPrecision = p[0] != 0;
        reinterpret_cast<L_Node*>(l_peek(host.runtime, parent._id))->highPrecision = p[0] != 0;
        ASSERT_EQ(bl_setNodeRotation(parent, {p[4], p[5], p[6]}), BL_OK);
        ASSERT_EQ(bl_setNodeScaling(parent, {p[7], p[8], p[9]}), BL_OK);
        ASSERT_EQ(bl_setNodePosition(parent, {p[10], p[11], p[12]}), BL_OK);
        const auto node = Node(light);
        ASSERT_EQ(bl_setNodePosition(node, {p[13], p[14], p[15]}), BL_OK);
        ASSERT_EQ(bl_setNodeParent(node, parent), BL_OK);
        float values[16]{};
        ASSERT_EQ(l_writeDirectionalLight(host.runtime, native, values), BL_OK);
        for (unsigned j = 0; j < 16; ++j)
        {
            EXPECT_FLOAT_EQ(values[j], data[i * 16 + j]) << j;
            if (values[j] == 0 && data[i * 16 + j] == 0)
                EXPECT_EQ(std::signbit(values[j]), std::signbit(data[i * 16 + j])) << j;
            EXPECT_NEAR(native->light.node.world.values[j], worlds[i * 16 + j], p[0] ? 2e-14 : 1e-6) << j;
        }
        const auto version = native->light.node.version;
        ASSERT_EQ(bl_setNodePosition(parent, {30, 40, 50}), BL_OK);
        float translated[16]{};
        ASSERT_EQ(l_writeDirectionalLight(host.runtime, native, translated), BL_OK);
        EXPECT_EQ(std::memcmp(values, translated, sizeof(values)), 0);
        EXPECT_NE(native->light.node.version, version);
    }
}

TEST(LiteDirectional, EveryFactoryAllocationFailureAndCrossFamilyChurn)
{
    size_t factoryCalls{};
    const bl_DirectionalLightOptions options{{0, -1, 0}, {}};
    for (size_t failure = 0; failure < 6; ++failure)
    {
        Allocations allocations;
        bl_RuntimeOptions runtimeOptions{};
        runtimeOptions.allocator = {&allocations, Allocations::Allocate, Allocations::Free};
        bl_Runtime* runtime{};
        ASSERT_EQ(bl_createRuntime(&runtimeOptions, &runtime), BL_OK);
        const size_t before = allocations.calls;
        allocations.fail = failure ? before + failure : 0;
        bl_DirectionalLight light{runtime, 123};
        const auto status = bl_createDirectionalLight(runtime, &options, &light);
        if (!failure) factoryCalls = allocations.calls - before;
        if (failure && failure <= factoryCalls)
        {
            EXPECT_EQ(status, BL_OUT_OF_MEMORY);
            EXPECT_EQ(light._id, 123u);
            allocations.fail = 0;
            ASSERT_EQ(bl_createDirectionalLight(runtime, &options, &light), BL_OK);
        }
        else EXPECT_EQ(status, BL_OK);
        ASSERT_EQ(bl_disposeRuntime(runtime), BL_OK);
        EXPECT_EQ(allocations.bytes, 0u);
        EXPECT_TRUE(allocations.live.empty());
    }
    EXPECT_EQ(factoryCalls, 3u);
    Allocations allocations;
    bl_RuntimeOptions runtimeOptions{};
    runtimeOptions.allocator = {&allocations, Allocations::Allocate, Allocations::Free};
    bl_Runtime* runtime{};
    ASSERT_EQ(bl_createRuntime(&runtimeOptions, &runtime), BL_OK);
    size_t warmBytes{};
    for (unsigned i = 0; i < 4096; ++i)
    {
        bl_SceneNode node{};
        bl_Light family{};
        if (i % 2)
        {
            bl_HemisphericLight light{};
            ASSERT_EQ(bl_createHemisphericLight(runtime, nullptr, &light), BL_OK);
            ASSERT_EQ(bl_hemisphericLightAsLight(light, &family), BL_OK);
        }
        else
        {
            bl_DirectionalLight light{};
            ASSERT_EQ(bl_createDirectionalLight(runtime, &options, &light), BL_OK);
            ASSERT_EQ(bl_directionalLightAsLight(light, &family), BL_OK);
        }
        ASSERT_EQ(bl_lightNode(family, &node), BL_OK);
        ASSERT_EQ(bl_disposeNode(node), BL_OK);
        if (i == 64) warmBytes = allocations.bytes;
        if (i > 64) EXPECT_LE(allocations.bytes, warmBytes + 512);
    }
    ASSERT_EQ(bl_disposeRuntime(runtime), BL_OK);
    EXPECT_EQ(allocations.bytes, 0u);
}
