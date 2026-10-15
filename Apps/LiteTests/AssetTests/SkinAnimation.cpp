#include "AssetMotionInternal.h"
#include <json.hpp>
#include <fstream>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

static size_t assertions;
static size_t live;

static void require(bool value, const char* message)
{
    ++assertions;
    if (!value)
    {
        fprintf(stderr, "Skin animation CPU: %s\n", message);
        exit(2);
    }
}

static void* allocate(void*, size_t bytes, size_t alignment)
{
    void* result = _aligned_malloc(bytes, alignment);
    live += result != nullptr;
    return result;
}

static void deallocate(void*, void* pointer, size_t, size_t)
{
    if (pointer)
    {
        --live;
        _aligned_free(pointer);
    }
}

static void numeric()
{
    float times[] = {1, 3};
    float values[] = {2, 4, 6, 6, 8, 10};
    L_AssetSampler sampler = {times, values, 2, 3, 0};
    float result[4] = {};
    for (double time : {0.0, 1.0, 2.0, 3.0, 4.0})
    {
        double expected = time <= 1 ? 2 : time >= 3 ? 6 : 4;
        require(l_sampleAssetAnimation(&sampler, time, false, result) == BL_OK &&
                    result[0] == expected,
                "independent LINEAR clamp/key/midpoint");
    }
    sampler.interpolation = 1;
    require(l_sampleAssetAnimation(&sampler, 2.999, false, result) == BL_OK && result[0] == 2,
            "STEP preceding exact key");
    require(l_sampleAssetAnimation(&sampler, 3, false, result) == BL_OK && result[0] == 6,
            "STEP exact final key");
    times[0] = 0;
    times[1] = 2;
    float cubic[] = {0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0};
    sampler.values = cubic;
    sampler.interpolation = 2;
    const float expectedCubic[] = {1, 1.4375f, 1.75f, 1.9375f, 2};
    for (size_t i = 0; i < 5; ++i)
    {
        require(l_sampleAssetAnimation(&sampler, i * .5, false, result) == BL_OK &&
                    result[1] == expectedCubic[i],
                "independent Hermite time-scaled derivatives");
    }
    float quaternion[] = {0, 0, 0, 1, 0, 0, 0, -1};
    sampler = {times, quaternion, 2, 4, 0};
    require(l_sampleAssetAnimation(&sampler, 1, true, result) == BL_OK && result[3] == 1,
            "shortest hemisphere q versus -q");
    quaternion[6] = -1;
    quaternion[7] = 0;
    require(l_sampleAssetAnimation(&sampler, 1, true, result) == BL_OK &&
                fabs(result[2] + sqrt(.5)) < 2e-6 && fabs(result[3] - sqrt(.5)) < 2e-6,
            "independent shortest-path90-degree midpoint");
    float cubicQuaternion[] = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, .5f, 0,
                               0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,   0};
    sampler = {times, cubicQuaternion, 2, 4, 2};
    require(l_sampleAssetAnimation(&sampler, 1, true, result) == BL_OK &&
                fabs(result[2] - .625 / sqrt(.640625)) < 2e-6 &&
                fabs(result[3] - .5 / sqrt(.640625)) < 2e-6,
            "cubic quaternion is normalized Hermite, not slerp");
    cubicQuaternion[10] = 0;
    cubicQuaternion[18] = 0;
    cubicQuaternion[19] = -1;
    require(l_sampleAssetAnimation(&sampler, 1, true, result) == BL_INVALID_ARGUMENT,
            "zero cubic interpolant rejected without hemisphere rewriting");
    bl_Quat zero = {};
    require(l_normalizeAssetQuaternion(&zero) == BL_INVALID_ARGUMENT, "zero quaternion invalid");
    bl_TransformNodeOptions pose = {{1, 2, 3}, {0, 0, sqrt(.5), sqrt(.5)}, {2, 3, 4}};
    bl_Mat4 matrix = {};
    l_composeAssetPose(&pose, &matrix);
    require(fabs(matrix.values[0]) < 2e-6 && matrix.values[1] == 2 && matrix.values[4] == -3 &&
                fabs(matrix.values[5]) < 2e-6 && matrix.values[10] == 4 && matrix.values[12] == 1 &&
                matrix.values[13] == 2 && matrix.values[14] == 3,
            "independent TRS column-major composition");
}

static void controls()
{
    bl_RuntimeOptions options = {};
    options.allocator = {nullptr, allocate, deallocate};
    bl_Runtime* runtime = nullptr;
    require(bl_createRuntime(&options, &runtime) == BL_OK, "CPU-only runtime");
    L_Engine engine = {};
    engine.runtime = runtime;
    L_Record* record = nullptr;
    require(l_record(runtime, sizeof(L_Asset), L_ASSET, 45, nullptr, &record) == BL_OK,
            "isolated Core ownership fixture, not glTF loading");
    auto* asset = reinterpret_cast<L_Asset*>(record);
    asset->engine = &engine;
    L_AssetMotion motion = {};
    asset->motion = &motion;
    bl_AnimationGroup groups[2] = {};
    motion.groups = groups;
    motion.groupCount = 2;
    L_AssetAnimation* first = nullptr;
    L_AssetAnimation* second = nullptr;
    require(l_createAssetAnimation(asset, 0, {"first", 5}, 0, 0, &first) == BL_OK &&
                l_createAssetAnimation(asset, 1, {"second", 6}, 0, 0, &second) == BL_OK,
            "stable Core group state without a renderer/parser");
    first->info.duration = 2;
    second->info.duration = 3;
    L_Node root = {};
    root.record.id = 123;
    asset->root = {runtime, 123};
    L_Node* members[] = {&root};
    L_Scene scene = {};
    scene.engine = &engine;
    scene.members = members;
    scene.memberCount = 1;
    bl_AssetContainer handle = {runtime, asset->record.id};
    bl_AnimationGroups view = {};
    require(bl_getAssetContainerAnimationGroups(handle, &view) == BL_OK && view.data == groups &&
                view.count == 2,
            "borrowed group order and identity");
    require(first->info.isPlaying && !first->info.isStopped && second->info.isStopped,
            "first autoplay, other clips retained stopped");
    ++engine.frameSerial;
    require(l_tickAssetMotion(asset, &scene, 500) == BL_OK && first->info.currentTime == .5,
            "native millisecond clock");
    require(l_tickAssetMotion(asset, &scene, 500) == BL_OK && first->info.currentTime == .5,
            "duplicate owner cannot double tick one engine frame");
    require(bl_pauseAnimation(groups[0]) == BL_OK && bl_playAnimation(groups[1]) == BL_BUSY,
            "paused evaluator remains exclusive");
    ++engine.frameSerial;
    require(l_tickAssetMotion(asset, &scene, 500) == BL_OK && first->info.currentTime == .5,
            "pause does not advance");
    require(bl_playAnimation(groups[0]) == BL_OK &&
                bl_setAnimationPlayback(groups[0], -1, true) == BL_OK,
            "negative speed accepted");
    ++engine.frameSerial;
    require(l_tickAssetMotion(asset, &scene, 1000) == BL_OK && first->info.currentTime == 1.5,
            "reverse wraps positive modulo");
    require(bl_goToFrame(groups[0], 120) == BL_OK && first->info.currentTime == 0 &&
                !first->info.isPlaying,
            "loop seek exact end wraps and pauses");
    require(bl_goToFrame(groups[0], -120) == BL_OK, "reverse exact loop endpoint");
    uint64_t zeroBits = 0;
    memcpy(&zeroBits, &first->info.currentTime, sizeof(zeroBits));
    require(zeroBits == 0, "positive modulo uses source positive zero");
    require(bl_setAnimationPlayback(groups[0], 1, false) == BL_OK &&
                bl_playAnimation(groups[0]) == BL_OK,
            "non-loop source properties");
    ++engine.frameSerial;
    require(l_tickAssetMotion(asset, &scene, 3000) == BL_OK && first->info.currentTime == 2 &&
                first->info.isPlaying,
            "non-loop endpoint clamps but stays playing");
    require(bl_stopAnimation(groups[0]) == BL_OK && first->info.isStopped &&
                first->info.currentTime == 0,
            "stop resets clock and disables evaluator");
    require(bl_selectAssetContainerAnimation(handle, 1) == BL_OK && first->info.isStopped &&
                second->info.isPlaying && !second->info.isStopped && second->info.currentTime == 0,
            "selection exclusive/rest reset");
    auto old = second->info;
    require(bl_goToFrame(groups[1], NAN) == BL_INVALID_ARGUMENT &&
                !memcmp(&old, &second->info, sizeof(old)) &&
                bl_selectAssetContainerAnimation(handle, 2) == BL_INVALID_ARGUMENT &&
                !memcmp(&old, &second->info, sizeof(old)),
            "invalid controls preserve entire state");
    bl_Error before = {};
    bl_getLastError(runtime, &before);
    bl_Status wrong = BL_OK;
    std::thread thread([&]() {
        bl_AnimationGroupInfo info = {};
        wrong = bl_getAnimationGroupInfo(groups[1], &info);
    });
    thread.join();
    bl_Error after = {};
    bl_getLastError(runtime, &after);
    require(wrong == BL_WRONG_THREAD && !memcmp(&before, &after, sizeof(before)),
            "wrong thread cannot mutate thread-affine diagnostics");
    second->info.duration = 0;
    require(bl_setAnimationPlayback(groups[1], 1e308, true) == BL_OK, "finite large speed");
    ++engine.frameSerial;
    require(l_tickAssetMotion(asset, &scene, 1e308) == BL_OK && second->info.currentTime == 0,
            "zero-duration clip does not multiply or modulo overflowing clocks");
    require(bl_disposeRuntime(runtime) == BL_OK && !live, "CPU fixture teardown0");
}

int main(int argc, char** argv)
{
    require(argc == 2, "ignored receipt path required");
    numeric();
    controls();
    nlohmann::json result = {{"state", "PASSED"},
                             {"assertions", assertions},
                             {"allocatorLive", live},
                             {"usesRaw6", false}};
    std::ofstream(argv[1]) << result.dump(2);
    printf("Native skin/animation CPU: %zu assertions, teardown0\n", assertions);
}
