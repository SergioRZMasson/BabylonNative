#include "TestRuntime.h"
#include "../../LitePlayground/Source/NativeAudio.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <chrono>
#include <thread>

TEST(LitePlatformAudio, ActualDeviceGraphAcceptsC99RoutingAndProducesSamples)
{
    struct Apartment
    {
        HRESULT initialized{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
        ~Apartment()
        {
            if (SUCCEEDED(initialized))
            {
                CoUninitialize();
            }
        }
    } apartment;
    ASSERT_TRUE(SUCCEEDED(apartment.initialized));
    {
        LitePlayground::NativeAudio host;
        const auto service = host.Service();
        TestRuntime native(&service);
        ASSERT_EQ(native.status, BL_OK);
        bl_AudioEngine engine{};
        const auto created = bl_createAudioEngineAsync(native.runtime, nullptr, &engine);
        if (created == BL_UNSUPPORTED)
        {
            GTEST_SKIP() << "No real output device; this is not an audible/offline pass.";
        }
        ASSERT_EQ(created, BL_OK);
        bl_HostAudioContext context{};
        const bl_AudioService* audio{};
        ASSERT_EQ(bl_getAudioContext(engine, &context, &audio), BL_OK);
        bl_HostAudioInfo info{};
        ASSERT_EQ(audio->getContextInfo(audio->userData, context, &info), BL_OK);
        EXPECT_FALSE(info.offline);
        EXPECT_TRUE(info.audibleOutputAvailable);
        bl_HostAudioNode input{};
        bl_HostAudioNode oscillator{};
        ASSERT_EQ(audio->createGain(audio->userData, context, &input), BL_OK);
        ASSERT_EQ(audio->createOscillator(audio->userData, context, &oscillator), BL_OK);
        ASSERT_EQ(audio->setOscillatorType(audio->userData, oscillator, BL_OSCILLATOR_TRIANGLE), BL_OK);
        ASSERT_EQ(audio->setParameter(audio->userData, input, BL_AUDIO_GAIN, 0.05, 0), BL_OK);
        ASSERT_EQ(audio->setParameter(audio->userData, oscillator, BL_AUDIO_FREQUENCY, 330, 0), BL_OK);
        ASSERT_EQ(audio->connect(audio->userData, oscillator, input), BL_OK);
        bl_AudioInputSource source{};
        ASSERT_EQ(bl_createSoundSourceAsync(engine, input, nullptr, &source), BL_OK);
        ASSERT_EQ(audio->start(audio->userData, oscillator, 0), BL_OK);
        ASSERT_EQ(bl_unlockAudioEngineAsync(engine), BL_OK);
        const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (host.NonzeroSamples() < 1000 && std::chrono::steady_clock::now() < timeout)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_GT(host.SubmittedFrames(), 1000u);
        EXPECT_GT(host.NonzeroSamples(), 1000u);
        EXPECT_TRUE(host.OutputAvailable());
        EXPECT_EQ(audio->setParameter(audio->userData, input, static_cast<bl_AudioParameter>(-1), 1, 0), BL_INVALID_ARGUMENT);
        bl_HostAudioBuffer unsupported{};
        EXPECT_EQ(audio->createBuffer(audio->userData, context, 2, 100, 48000, &unsupported), BL_UNSUPPORTED);
        EXPECT_EQ(audio->start(audio->userData, input, 0), BL_INVALID_ARGUMENT);
        EXPECT_EQ(audio->stop(audio->userData, oscillator, 0), BL_OK);
        EXPECT_EQ(bl_disposeSoundSource(source), BL_OK);
        EXPECT_EQ(bl_disposeAudioEngine(engine), BL_OK);
        EXPECT_FALSE(host.OutputAvailable());
    }
}
