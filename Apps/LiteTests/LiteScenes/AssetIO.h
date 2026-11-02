#pragma once

#include <babylon_lite.h>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace LiteScenes
{
    struct Assets
    {
        std::unordered_map<std::string, std::filesystem::path> paths;
        size_t reads{};
        size_t decodes{};
        size_t bytes{};
        double readMs{};
        double decodeMs{};
    };

    void ReadMap(Assets& assets, const std::filesystem::path& path);
    bl_IOService IO(Assets* assets);
}
