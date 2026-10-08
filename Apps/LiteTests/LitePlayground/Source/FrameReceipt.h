#pragma once

#include <bgfx/bgfx.h>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace LitePlayground
{
    struct ExecutionTimes
    {
        uint64_t process{};
        uint64_t apiThread{};
    };

    struct FrameSample
    {
        uint32_t frame{};
        double platformUserMs{};
        double nativeIncludingCallbacksMs{};
        double userCallbacksMs{};
        double uiUpdateMs{};
        double uiRenderMs{};
        double presentMs{};
        double hostApiFrameMs{};
        double completeHostWallMs{};
        double processCpuCompleteHostMs{};
        double osThreadCpuMs{};
        double processCpuMs{};
        double apiThreadCpuMs{};
        double renderSubmitWallMs{};
        bool gpuAvailable{};
        uint32_t gpuFrame{};
        int64_t gpuBegin{};
        int64_t gpuEnd{};
        int64_t gpuFrequency{};
        double gpuMs{};
    };

    ExecutionTimes ReadExecutionTimes();
    void WriteFrameReceipt(const std::filesystem::path& path, uint32_t warmup, uint32_t measure,
                           bool retainedUi, const std::vector<FrameSample>& samples,
                           const std::string& uiBefore, const std::string& uiAfter, int result);
}
