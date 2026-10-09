#include "TestRuntime.h"
#include "../../../../Core/LiteLayer/Source/ArcRotateInternal.h"
#include "../../../../Core/LiteLayer/Source/HemisphericLightInternal.h"
#include <cmath>
#include <limits>

TEST(LiteArc, DataOnlyDefaultsAndOriginalOrbitWorld)
{
    TestRuntime host;
    ASSERT_EQ(host.status, BL_OK);
    bl_ArcRotateCamera camera{};
    ASSERT_EQ(bl_createArcRotateCamera(host.runtime, -std::acos(-1.0) / 2, 1.1, 5, {}, &camera), BL_OK);
    bl_ArcRotateCameraProperties properties{};
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &properties), BL_OK);
    EXPECT_DOUBLE_EQ(properties.fov, .8);
    EXPECT_DOUBLE_EQ(properties.nearPlane, .1);
    EXPECT_DOUBLE_EQ(properties.farPlane, 1000);
    EXPECT_DOUBLE_EQ(properties.angularSensibility, 1000);
    EXPECT_DOUBLE_EQ(properties.panningSensibility, 50);
    EXPECT_DOUBLE_EQ(properties.wheelPrecision, 3);
    bl_SceneNode node{};
    ASSERT_EQ(bl_arcRotateCameraNode(camera, &node), BL_OK);
    bl_Mat4 world{};
    uint64_t version{};
    ASSERT_EQ(bl_getNodeWorldMatrix(node, &world, &version), BL_OK);
    EXPECT_NEAR(world.values[13], 5 * std::cos(1.1), 2e-7);
    EXPECT_NEAR(world.values[14], -5 * std::sin(1.1), 3e-7);
    properties.fov = 1.0;
    ASSERT_EQ(bl_setArcRotateCameraProperties(camera, &properties), BL_OK);
    uint64_t projectedVersion{};
    ASSERT_EQ(bl_getNodeWorldMatrix(node, &world, &projectedVersion), BL_OK);
    EXPECT_EQ(version, projectedVersion);
    EXPECT_EQ(bl_setNodePosition(node, {1, 2, 3}), BL_UNSUPPORTED);
    bl_Camera family{};
    ASSERT_EQ(bl_arcRotateCameraAsCamera(camera, &family), BL_OK);
    bl_FreeCamera untouched{host.runtime, 99};
    EXPECT_EQ(bl_cameraAsFreeCamera(family, &untouched), BL_INVALID_HANDLE);
    EXPECT_EQ(untouched._id, 99u);
    bl_TransformNode parent{};
    ASSERT_EQ(bl_createTransformNode(host.runtime, {}, nullptr, &parent), BL_OK);
    ASSERT_EQ(bl_setNodeScaling(parent, {2, 3, 4}), BL_OK);
    ASSERT_EQ(bl_setNodePosition(parent, {7, 8, 9}), BL_OK);
    ASSERT_EQ(bl_setNodeParent(node, parent), BL_OK);
    ASSERT_EQ(bl_getNodeWorldMatrix(node, &world, &version), BL_OK);
    auto* native = reinterpret_cast<L_Node*>(l_peek(host.runtime, node._id));
    bl_Mat4 view{};
    l_arcView(native, &view);
    for (unsigned column = 0; column < 3; ++column)
    {
        for (unsigned row = 0; row < 3; ++row)
        {
            EXPECT_EQ(view.values[column * 4 + row], world.values[row * 4 + column]);
        }
        const double translation = -(world.values[column * 4] * world.values[12] +
            world.values[column * 4 + 1] * world.values[13] +
            world.values[column * 4 + 2] * world.values[14]);
        EXPECT_EQ(view.values[12 + column], static_cast<double>(static_cast<float>(translation)));
    }
}

TEST(LiteArc, ImmediateMaskedLimitsAndOlderDisposer)
{
    TestRuntime host;
    bl_ArcRotateCamera camera{};
    ASSERT_EQ(bl_createArcRotateCamera(host.runtime, 0, 1, 5, {}, &camera), BL_OK);
    bl_ArcRotateCameraLimitPatch patch{};
    patch.fields = BL_LIMIT_UPPER_RADIUS;
    patch.values.upperRadiusLimit = {true, 3};
    bl_CameraLimitToken first{};
    ASSERT_EQ(bl_setCameraLimits(camera, &patch, &first), BL_OK);
    bl_ArcRotateCameraProperties properties{};
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &properties), BL_OK);
    EXPECT_EQ(properties.radius, 3);
    patch.values.upperRadiusLimit.value = 2;
    bl_CameraLimitToken second{};
    ASSERT_EQ(bl_setCameraLimits(camera, &patch, &second), BL_OK);
    ASSERT_EQ(bl_removeCameraLimits(first), BL_OK);
    properties.radius = 9;
    properties.inertialRadiusOffset = 4;
    ASSERT_EQ(bl_setArcRotateCameraProperties(camera, &properties), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &properties), BL_OK);
    EXPECT_EQ(properties.radius, 2);
    EXPECT_EQ(properties.inertialRadiusOffset, 0);
    ASSERT_EQ(bl_removeCameraLimits(second), BL_OK);
    ASSERT_EQ(bl_removeCameraLimits(second), BL_OK);
    properties.radius = -5;
    ASSERT_EQ(bl_setArcRotateCameraProperties(camera, &properties), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &properties), BL_OK);
    EXPECT_EQ(properties.radius, -5);
}

TEST(LiteArc, SourcePointerWheelAndPinchWithoutScene)
{
    TestRuntime host;
    bl_ArcRotateCamera camera{};
    ASSERT_EQ(bl_createArcRotateCamera(host.runtime, 0, 1, 5, {}, &camera), BL_OK);
    bl_ArcRotateControl control{};
    ASSERT_EQ(bl_attachControl(camera, {}, nullptr, &control), BL_OK);
    bl_ArcRotateInput input{};
    input.kind = BL_ARC_POINTER_DOWN;
    input.clientX = 10;
    input.clientY = 20;
    bl_ArcRotateInputEffects effects{};
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    EXPECT_TRUE(effects.capturePointer);
    input.kind = BL_ARC_POINTER_MOVE;
    input.clientX = 110;
    input.clientY = 70;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    bl_ArcRotateCameraProperties p{};
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_DOUBLE_EQ(p.alpha, 0);
    EXPECT_DOUBLE_EQ(p.inertialAlphaOffset, -.1);
    EXPECT_DOUBLE_EQ(p.inertialBetaOffset, -.05);
    input.kind = BL_ARC_WHEEL;
    input.deltaY = 120;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_DOUBLE_EQ(p.inertialRadiusOffset, -.2);
    bl_ArcRotateTouch touches[]{{1, 0, 0}, {2, 100, 0}, {3, 1000, 1000}};
    input.kind = BL_ARC_TOUCH_START;
    input.changedTouches = touches;
    input.changedTouchCount = 3;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    touches[1].clientX = 200;
    input.kind = BL_ARC_TOUCH_MOVE;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_DOUBLE_EQ(p.radius, 2.5);
    bl_SceneNode node{};
    ASSERT_EQ(bl_arcRotateCameraNode(camera, &node), BL_OK);
    EXPECT_EQ(bl_disposeNode(node), BL_BUSY);
    ASSERT_EQ(bl_detachControl(control), BL_OK);
    ASSERT_EQ(bl_detachControl(control), BL_OK);
    ASSERT_EQ(bl_disposeNode(node), BL_OK);
}

TEST(LiteHemispheric, OriginalWorldDirectionPackAndExplicitScalarDirty)
{
    TestRuntime host;
    bl_HemisphericLight light{};
    ASSERT_EQ(bl_createHemisphericLight(host.runtime, nullptr, &light), BL_OK);
    bl_Light family{};
    ASSERT_EQ(bl_hemisphericLightAsLight(light, &family), BL_OK);
    bl_SceneNode node{};
    ASSERT_EQ(bl_lightNode(family, &node), BL_OK);
    bl_HemisphericLightProperties p{};
    ASSERT_EQ(bl_getHemisphericLightProperties(light, &p), BL_OK);
    p.direction = {0, 7, 0};
    p.intensity = -2;
    ASSERT_EQ(bl_setHemisphericLightProperties(light, &p), BL_OK);
    auto* native = reinterpret_cast<L_HemisphericLight*>(l_peek(host.runtime, light._id));
    EXPECT_EQ(native->dataVersion, 1u);
    float data[16]{};
    ASSERT_EQ(l_writeHemisphericLight(host.runtime, native, data), BL_OK);
    EXPECT_EQ(data[1], 1);
    EXPECT_EQ(data[3], 3);
    EXPECT_EQ(data[4], -2);
    EXPECT_EQ(data[7], 0);
    p.intensity = 4;
    ASSERT_EQ(bl_setHemisphericLightProperties(light, &p), BL_OK);
    EXPECT_EQ(native->dataVersion, 1u);
    ASSERT_EQ(bl_markLightUboDirty(family), BL_OK);
    EXPECT_EQ(native->dataVersion, 2u);
    ASSERT_EQ(bl_setLightIntensity(family, 5), BL_OK);
    EXPECT_EQ(native->dataVersion, 3u);
    ASSERT_EQ(bl_setLightIntensity(family, 5), BL_OK);
    EXPECT_EQ(native->dataVersion, 3u);
    EXPECT_EQ(bl_setLightIntensity(family, std::numeric_limits<double>::infinity()), BL_INVALID_ARGUMENT);
    bl_Error error{};
    ASSERT_EQ(bl_getLastError(host.runtime, &error), BL_OK);
    EXPECT_EQ(error.sourceErrorCode, 134u);
}

TEST(LiteArc, RawLimitsEqualAndNonOrbitWritesPreserveOriginalHookSemantics)
{
    TestRuntime host;
    bl_ArcRotateCamera camera{};
    ASSERT_EQ(bl_createArcRotateCamera(host.runtime, 0, 1, 5, {}, &camera), BL_OK);
    bl_ArcRotateCameraLimitPatch patch{};
    patch.fields = BL_LIMIT_UPPER_RADIUS;
    patch.values.upperRadiusLimit = {true, 3};
    bl_CameraLimitToken token{};
    ASSERT_EQ(bl_setCameraLimits(camera, &patch, &token), BL_OK);
    bl_SceneNode node{};
    ASSERT_EQ(bl_arcRotateCameraNode(camera, &node), BL_OK);
    bl_Mat4 world{};
    uint64_t version{};
    ASSERT_EQ(bl_getNodeWorldMatrix(node, &world, &version), BL_OK);
    patch.values.upperRadiusLimit.value = 2;
    ASSERT_EQ(bl_setArcRotateCameraLimitFields(camera, &patch), BL_OK);
    bl_ArcRotateCameraProperties p{};
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_EQ(p.radius, 3);
    ASSERT_EQ(bl_setArcRotateCameraProperties(camera, &p), BL_OK);
    uint64_t after{};
    ASSERT_EQ(bl_getNodeWorldMatrix(node, &world, &after), BL_OK);
    EXPECT_EQ(version, after);
    p.fov = 1;
    p.inertialRadiusOffset = 4;
    ASSERT_EQ(bl_setArcRotateCameraProperties(camera, &p), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_EQ(p.radius, 3);
    EXPECT_EQ(p.inertialRadiusOffset, 4);
    p.target.x = 1;
    ASSERT_EQ(bl_setArcRotateCameraProperties(camera, &p), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_EQ(p.radius, 3);
    p.alpha = .1;
    ASSERT_EQ(bl_setArcRotateCameraProperties(camera, &p), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_EQ(p.radius, 2);
    EXPECT_EQ(p.inertialRadiusOffset, 0);
    patch.fields |= BL_LIMIT_LOWER_RADIUS;
    patch.values.lowerRadiusLimit = {true, 4};
    EXPECT_EQ(bl_setArcRotateCameraLimitFields(camera, &patch), BL_INVALID_ARGUMENT);
    ASSERT_EQ(bl_removeCameraLimits(token), BL_OK);
    p.radius = 9;
    ASSERT_EQ(bl_setArcRotateCameraProperties(camera, &p), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_EQ(p.radius, 9);
}

TEST(LiteArc, OptionsReplaceAtomicallyWithoutGestureResetOrInputReentry)
{
    TestRuntime host;
    bl_ArcRotateCamera camera{};
    ASSERT_EQ(bl_createArcRotateCamera(host.runtime, 0, 1, 5, {}, &camera), BL_OK);
    bl_ArcRotateControl control{};
    ASSERT_EQ(bl_attachControl(camera, {}, nullptr, &control), BL_OK);
    bl_ArcRotateControlOptions options{};
    EXPECT_EQ(bl_setArcRotateControlOptions(control, nullptr), BL_INVALID_ARGUMENT);
    options.keyboard = true;
    EXPECT_EQ(bl_setArcRotateControlOptions(control, &options), BL_UNSUPPORTED);
    options.keyboard = false;
    options.primaryButton = BL_ARC_ACTION_PAN;
    ASSERT_EQ(bl_setArcRotateControlOptions(control, &options), BL_OK);
    bl_ArcRotateInput input{};
    input.kind = BL_ARC_POINTER_DOWN;
    bl_ArcRotateInputEffects effects{};
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    options.primaryButton = BL_ARC_ACTION_ROTATE;
    ASSERT_EQ(bl_setArcRotateControlOptions(control, &options), BL_OK);
    input.kind = BL_ARC_POINTER_MOVE;
    input.clientX = 100;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    bl_ArcRotateCameraProperties p{};
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_EQ(p.inertialAlphaOffset, 0);
    EXPECT_EQ(p.inertialPanningX, -2);
    struct Probe { bl_ArcRotateControl control; bl_Status result; unsigned calls{}; } probe{control, BL_OK};
    options.userData = &probe;
    options.shouldHandlePointerDown = [](void* user, const bl_ArcRotateInput*) {
        auto* p = static_cast<Probe*>(user);
        ++p->calls;
        bl_ArcRotateControlOptions defaults{};
        p->result = bl_setArcRotateControlOptions(p->control, &defaults);
        return true;
    };
    ASSERT_EQ(bl_setArcRotateControlOptions(control, &options), BL_OK);
    input.kind = BL_ARC_POINTER_DOWN;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    EXPECT_EQ(probe.result, BL_BUSY);
    input.kind = BL_ARC_POINTER_MOVE;
    input.clientX = 200;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_DOUBLE_EQ(p.inertialAlphaOffset, -.1);
    EXPECT_EQ(bl_setArcRotateControlOptions(control, nullptr), BL_INVALID_ARGUMENT);
    options.keyboard = true;
    EXPECT_EQ(bl_setArcRotateControlOptions(control, &options), BL_UNSUPPORTED);
    options.keyboard = false;
    options.primaryButton = static_cast<bl_ArcRotatePointerAction>(999);
    EXPECT_EQ(bl_setArcRotateControlOptions(control, &options), BL_INVALID_ARGUMENT);
    input.kind = BL_ARC_POINTER_DOWN;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    EXPECT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.result, BL_BUSY);
    input.kind = BL_ARC_POINTER_MOVE;
    input.clientX = 250;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    ASSERT_EQ(bl_getArcRotateCameraProperties(camera, &p), BL_OK);
    EXPECT_NEAR(p.inertialAlphaOffset, -.15, 1e-15);
    bl_ArcRotateControlOptions defaults{};
    ASSERT_EQ(bl_setArcRotateControlOptions(control, &defaults), BL_OK);
    input.kind = BL_ARC_POINTER_DOWN;
    ASSERT_EQ(bl_processArcRotateInput(control, &input, &effects), BL_OK);
    EXPECT_EQ(probe.calls, 2u);
}

TEST(LiteArc, FactoryFailuresAndControlLimitStoragePlateau)
{
    struct Allocator
    {
        size_t live{};
        int remaining{-1};
        static void* Allocate(void* user, size_t bytes, size_t alignment)
        {
            auto& a = *static_cast<Allocator*>(user);
            if (a.remaining == 0) return nullptr;
            if (a.remaining > 0) --a.remaining;
            auto* p = TestRuntime::Allocate(nullptr, bytes, alignment);
            if (p) a.live += bytes;
            return p;
        }
        static void Free(void* user, void* p, size_t bytes, size_t alignment)
        {
            static_cast<Allocator*>(user)->live -= bytes;
            TestRuntime::Deallocate(nullptr, p, bytes, alignment);
        }
    } allocator;
    bl_RuntimeOptions options{};
    options.allocator = {&allocator, Allocator::Allocate, Allocator::Free};
    bl_Runtime* runtime{};
    ASSERT_EQ(bl_createRuntime(&options, &runtime), BL_OK);
    for (int failure = 0; failure < 5; ++failure)
    {
        allocator.remaining = failure;
        bl_ArcRotateCamera camera{runtime, 123};
        const auto status = bl_createArcRotateCamera(runtime, 0, 1, 5, {}, &camera);
        if (status == BL_OK)
        {
            bl_SceneNode node{};
            ASSERT_EQ(bl_arcRotateCameraNode(camera, &node), BL_OK);
            ASSERT_EQ(bl_disposeNode(node), BL_OK);
        }
        else
        {
            EXPECT_EQ(status, BL_OUT_OF_MEMORY);
            EXPECT_EQ(camera._id, 123u);
        }
    }
    allocator.remaining = -1;
    size_t warmed{};
    for (unsigned i = 0; i < 2000; ++i)
    {
        bl_ArcRotateCamera camera{};
        ASSERT_EQ(bl_createArcRotateCamera(runtime, 0, 1, 5, {}, &camera), BL_OK);
        bl_ArcRotateControl control{};
        ASSERT_EQ(bl_attachControl(camera, {}, nullptr, &control), BL_OK);
        bl_ArcRotateCameraLimitPatch patch{};
        bl_CameraLimitToken token{};
        ASSERT_EQ(bl_setCameraLimits(camera, &patch, &token), BL_OK);
        ASSERT_EQ(bl_removeCameraLimits(token), BL_OK);
        ASSERT_EQ(bl_detachControl(control), BL_OK);
        bl_SceneNode node{};
        ASSERT_EQ(bl_arcRotateCameraNode(camera, &node), BL_OK);
        ASSERT_EQ(bl_disposeNode(node), BL_OK);
        bl_HemisphericLight light{};
        ASSERT_EQ(bl_createHemisphericLight(runtime, nullptr, &light), BL_OK);
        bl_Light family{};
        ASSERT_EQ(bl_hemisphericLightAsLight(light, &family), BL_OK);
        ASSERT_EQ(bl_lightNode(family, &node), BL_OK);
        ASSERT_EQ(bl_disposeNode(node), BL_OK);
        bl_StandardMaterial material{};
        ASSERT_EQ(bl_createStandardMaterial(runtime, &material), BL_OK);
        ASSERT_EQ(bl_disposeStandardMaterial(material), BL_OK);
        if (i == 20) warmed = allocator.live;
        if (i > 20) EXPECT_LE(allocator.live, warmed);
    }
    ASSERT_EQ(bl_disposeRuntime(runtime), BL_OK);
    EXPECT_EQ(allocator.live, 0u);
}
