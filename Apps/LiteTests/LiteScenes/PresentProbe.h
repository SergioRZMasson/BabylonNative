#pragma once

#include <cstdint>
#include <filesystem>

namespace LiteScenesProbe
{
    enum Flags : uint32_t
    {
        None = 0
    };

    void Install(uint32_t flags);
    void MarkSourceFrame();
    void Write(const std::filesystem::path& path);
    void WriteBaseline(const double* samples, size_t count);
    void Shutdown();
}
