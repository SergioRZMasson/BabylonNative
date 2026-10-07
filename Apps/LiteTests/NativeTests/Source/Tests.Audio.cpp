#include "TestRuntime.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

struct bl_HostAudioContextTag
{
    bl_HostAudioInfo info{BL_AUDIO_RUNNING, 2, 48000, true, false};
};

struct bl_HostAudioNodeTag
{
    std::array<double, 3> parameters{1, 440, 1};
    std::vector<bl_HostAudioNode> targets;
    bool released{};
};

namespace
{
    // Contract-only host double. Explicitly offline and never audible.
    struct AudioHost
    {
        bl_HostAudioContextTag context;
        bl_HostAudioNodeTag destination;
        std::vector<std::unique_ptr<bl_HostAudioNodeTag>> nodes;
        std::vector<float> curve;
        double curveStart{}, curveDuration{};
        uint32_t resumes{};

        bl_AudioService Service()
        {
            bl_AudioService service{};
            service.userData = this;
            service.createContext = [](void* user, bl_HostAudioContext* result) {
                *result = &static_cast<AudioHost*>(user)->context;
                return BL_OK;
            };
            service.closeContext = [](void*, bl_HostAudioContext context) {
                context->info.state = BL_AUDIO_CLOSED;
                return BL_OK;
            };
            service.resumeContext = [](void* user, bl_HostAudioContext context) {
                ++static_cast<AudioHost*>(user)->resumes;
                context->info.state = BL_AUDIO_RUNNING;
                return BL_OK;
            };
            service.getContextInfo = [](void*, bl_HostAudioContext context, bl_HostAudioInfo* info) {
                *info = context->info;
                return BL_OK;
            };
            service.getDestination = [](void* user, bl_HostAudioContext, bl_HostAudioNode* result) {
                *result = &static_cast<AudioHost*>(user)->destination;
                return BL_OK;
            };
            service.createGain = [](void* user, bl_HostAudioContext, bl_HostAudioNode* result) {
                auto& host = *static_cast<AudioHost*>(user);
                host.nodes.push_back(std::make_unique<bl_HostAudioNodeTag>());
                *result = host.nodes.back().get();
                return BL_OK;
            };
            service.connect = [](void*, bl_HostAudioNode source, bl_HostAudioNode target) {
                source->targets.push_back(target);
                return BL_OK;
            };
            service.disconnect = [](void*, bl_HostAudioNode node) {
                node->targets.clear();
                return BL_OK;
            };
            service.setParameter = [](void*, bl_HostAudioNode node, bl_AudioParameter parameter, double value, double) {
                node->parameters[static_cast<size_t>(parameter)] = value;
                return BL_OK;
            };
            service.getParameter = [](void*, bl_HostAudioNode node, bl_AudioParameter parameter, double* value) {
                *value = node->parameters[static_cast<size_t>(parameter)];
                return BL_OK;
            };
            service.cancelScheduledParameter = [](void*, bl_HostAudioNode, bl_AudioParameter, double) {
                return BL_OK;
            };
            service.setParameterCurve = [](void* user, bl_HostAudioNode node, bl_AudioParameter parameter,
                                            bl_F32Span values, double start, double duration) {
                auto& host = *static_cast<AudioHost*>(user);
                host.curve.assign(values.data, values.data + values.count);
                host.curveStart = start;
                host.curveDuration = duration;
                node->parameters[static_cast<size_t>(parameter)] = host.curve.back();
                return BL_OK;
            };
            service.releaseNode = [](void*, bl_HostAudioNode node) { node->released = true; };
            return service;
        }
    };

    std::vector<float> ReadCurve(const char* name)
    {
        const auto path = std::filesystem::path(BL_ORACLE_DIRECTORY) / (std::string(name) + ".bin");
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            ADD_FAILURE() << "Missing official audio curve fixture: " << path.string();
            return {};
        }
        const auto bytes = stream.tellg();
        std::vector<float> result(static_cast<size_t>(bytes) / sizeof(float));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(result.data()), bytes);
        if (!stream)
        {
            ADD_FAILURE() << "Could not read official audio curve fixture.";
            return {};
        }
        return result;
    }
}

TEST(LiteAudio, MatchesNineOfficialFloat32VolumeCurvesWithoutClaimingAudibleOutput)
{
    struct Case
    {
        const char* name;
        bl_AudioRampShape shape;
        double from, to;
    };
    const Case cases[]{
        {"audio-linear-0-1", BL_AUDIO_RAMP_LINEAR, 0, 1},
        {"audio-linear-1-0", BL_AUDIO_RAMP_LINEAR, 1, 0},
        {"audio-linear-0.4-0.4", BL_AUDIO_RAMP_LINEAR, 0.4, 0.4},
        {"audio-exponential-0-1", BL_AUDIO_RAMP_EXPONENTIAL, 0, 1},
        {"audio-exponential-1-0", BL_AUDIO_RAMP_EXPONENTIAL, 1, 0},
        {"audio-exponential-0.4-0.4", BL_AUDIO_RAMP_EXPONENTIAL, 0.4, 0.4},
        {"audio-logarithmic-0-1", BL_AUDIO_RAMP_LOGARITHMIC, 0, 1},
        {"audio-logarithmic-1-0", BL_AUDIO_RAMP_LOGARITHMIC, 1, 0},
        {"audio-logarithmic-0.4-0.4", BL_AUDIO_RAMP_LOGARITHMIC, 0.4, 0.4},
    };
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.name);
        AudioHost host;
        const auto service = host.Service();
        TestRuntime runtime(&service);
        ASSERT_EQ(runtime.status, BL_OK);
        bl_AudioEngine engine{};
        bl_AudioEngineOptions options{};
        options.parameterRampDuration = {true, 1};
        ASSERT_EQ(bl_createAudioEngineAsync(runtime.runtime, &options, &engine), BL_OK);
        const bl_RampOptions immediate{{}, BL_AUDIO_RAMP_NONE};
        ASSERT_EQ(bl_setMasterVolume(engine, test.from, &immediate), BL_OK);
        const bl_RampOptions ramp{{true, 0.05}, test.shape};
        ASSERT_EQ(bl_setMasterVolume(engine, test.to, &ramp), BL_OK);
        const auto expected = ReadCurve(test.name);
        ASSERT_EQ(host.curve.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_NEAR(host.curve[i], expected[i], 2e-7) << "sample " << i;
        }
        EXPECT_EQ(host.curveStart, 2);
        EXPECT_EQ(host.curveDuration, 1);
        bl_AudioEngineInfo info{};
        ASSERT_EQ(bl_getAudioEngineInfo(engine, &info), BL_OK);
        EXPECT_TRUE(info.context.offline);
        EXPECT_FALSE(info.context.audibleOutputAvailable);
        EXPECT_EQ(bl_disposeAudioEngine(engine), BL_OK);
    }
}

TEST(LiteAudio, MissingHostIsExplicitlyUnavailable)
{
    TestRuntime runtime;
    ASSERT_EQ(runtime.status, BL_OK);
    bl_AudioEngine engine{};
    EXPECT_EQ(bl_createAudioEngineAsync(runtime.runtime, nullptr, &engine), BL_AUDIO_UNAVAILABLE);
}

TEST(LiteAudio, SuspendedContextRetryUsesHostMonotonicTimeNotFrozenAudioClock)
{
    AudioHost host;
    const auto service = host.Service();
    TestRuntime runtime(&service);
    ASSERT_EQ(runtime.status, BL_OK);
    bl_AudioEngine engine{};
    bl_AudioEngineOptions options{};
    options.resumeOnInteraction = BL_BOOL_FALSE;
    options.resumeOnPauseRetryInterval = {true, 1000};
    ASSERT_EQ(bl_createAudioEngineAsync(runtime.runtime, &options, &engine), BL_OK);
    host.context.info.offline = false;
    host.context.info.state = BL_AUDIO_STATE_SUSPENDED;
    const double frozenTime = host.context.info.currentTime;
    ASSERT_EQ(bl_pollAudioEngine(engine, 5000), BL_OK);
    const auto resumes = host.resumes;
    host.context.info.state = BL_AUDIO_STATE_SUSPENDED;
    ASSERT_EQ(bl_pollAudioEngine(engine, 5500), BL_OK);
    EXPECT_EQ(host.resumes, resumes);
    ASSERT_EQ(bl_pollAudioEngine(engine, 6001), BL_OK);
    EXPECT_GT(host.resumes, resumes);
    EXPECT_EQ(host.context.info.currentTime, frozenTime);
    EXPECT_EQ(bl_disposeAudioEngine(engine), BL_OK);
}
