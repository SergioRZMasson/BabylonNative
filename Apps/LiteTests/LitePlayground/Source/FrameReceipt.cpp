#include "FrameReceipt.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace
{
    uint64_t Ticks(FILETIME value)
    {
        return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    }
}

namespace LitePlayground
{
    ExecutionTimes ReadExecutionTimes()
    {
        FILETIME created{};
        FILETIME exited{};
        FILETIME kernel{};
        FILETIME user{};
        ExecutionTimes times{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        {
            throw std::runtime_error("GetProcessTimes failed.");
        }
        times.process = Ticks(kernel) + Ticks(user);
        if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        {
            throw std::runtime_error("GetThreadTimes failed.");
        }
        times.apiThread = Ticks(kernel) + Ticks(user);
        return times;
    }

    void WriteFrameReceipt(const std::filesystem::path& path, uint32_t warmup, uint32_t measure,
                           bool retainedUi, const std::vector<FrameSample>& samples,
                           const std::string& uiBefore, const std::string& uiAfter, int result)
    {
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream file(path);
        file << std::setprecision(17);
        file
            << "{\"schema\":\"lite-js-rmlui-frames-v1\","
               "\"authority\":\"Babylon Lite 1.32.0\","
               "\"sourcePin\":\"2e064d88ec7422af946f8ec7f089ac6519f99295\","
               "\"rmlUiPin\":\"b7b4a0688262832eacf3b9abb41f8bbe73868af8\","
               "\"seed\":1337,\"radius\":6,\"warmupFrames\":"
            << warmup << ",\"measureFrames\":" << measure << ",\"sampleCount\":" << samples.size()
            << ",\"fixedDeltaMs\":" << 1000.0 / 60
            << ",\"retainedUi\":" << (retainedUi ? "true" : "false")
            << ",\"physicalInputAccepted\":false,\"measuredCaptures\":false,"
               "\"engineImplementation\":\"handwritten Core/LiteLayer C99\","
               "\"userRuntime\":\"JsRuntimeHost V8\","
               "\"present\":\"BORROWED bgfx D3D11; once after 3D/UI\","
               "\"gc\":\"normal V8; included in enclosing user/API spans, not separately instrumented\","
               "\"worldObservability\":\"original JS private world uninstrumented; seed/radius/source identity only\","
               "\"width\":1280,\"height\":720,\"msaa\":4,\"vsync\":false,"
               "\"renderThreadCpuMs\":null,"
               "\"timingNotes\":\"nativeIncludingCallbacksMs includes JS beforeRender; "
               "nativeExcludingCallbacksMs subtracts that nested wall span; "
               "native calls made by user callbacks remain inside userCallbacksMs, "
               "so this is NOT total engine-algorithm execution time. "
               "renderSubmitWallMs is bgfx render-thread wall duration, NOT execution CPU. "
               "processCpuMs/API-thread CPU are Windows execution counters with 100ns units "
               "but scheduler-granularity increments. Complete-host spans include OS pump and "
               "dispatch round trip; their CPU spans overlap API spans, do not sum them. "
               "GPU IDs deduplicated; timestamps are decimal strings to preserve 64-bit identity. "
               "unavailable/duplicate queries are null, not zero.\","
               "\"uiBefore\":"
            << (uiBefore.empty() ? "null" : uiBefore)
            << ",\"uiAfter\":" << (uiAfter.empty() ? "null" : uiAfter) << ",\"result\":" << result
            << ",\"samples\":[";
        bool first{true};
        for (const auto& sample : samples)
        {
            if (!first)
            {
                file << ',';
            }
            first = false;
            file << "{\"frame\":" << sample.frame << ",\"platformUserMs\":" << sample.platformUserMs
                 << ",\"nativeIncludingCallbacksMs\":" << sample.nativeIncludingCallbacksMs
                 << ",\"userCallbacksMs\":" << sample.userCallbacksMs
                 << ",\"nativeExcludingCallbacksMs\":"
                 << sample.nativeIncludingCallbacksMs - sample.userCallbacksMs
                 << ",\"uiUpdateMs\":" << sample.uiUpdateMs
                 << ",\"uiRenderMs\":" << sample.uiRenderMs << ",\"presentMs\":" << sample.presentMs
                 << ",\"hostApiFrameMs\":" << sample.hostApiFrameMs
                 << ",\"completeHostWallMs\":" << sample.completeHostWallMs
                 << ",\"processCpuCompleteHostMs\":" << sample.processCpuCompleteHostMs
                 << ",\"osThreadCpuMs\":" << sample.osThreadCpuMs
                 << ",\"processCpuMs\":" << sample.processCpuMs
                 << ",\"apiThreadCpuMs\":" << sample.apiThreadCpuMs
                 << ",\"renderSubmitWallMs\":" << sample.renderSubmitWallMs << ",\"gpu\":";
            if (sample.gpuAvailable)
            {
                file << "{\"frameId\":" << sample.gpuFrame << ",\"begin\":\"" << sample.gpuBegin
                     << "\",\"end\":\"" << sample.gpuEnd
                     << "\",\"frequency\":" << sample.gpuFrequency << ",\"ms\":" << sample.gpuMs
                     << '}';
            }
            else
            {
                file << "null";
            }
            file << '}';
        }
        file << "]}\n";
        if (!file)
        {
            throw std::runtime_error("Could not persist JS frame receipt: " + path.string());
        }
    }
}
