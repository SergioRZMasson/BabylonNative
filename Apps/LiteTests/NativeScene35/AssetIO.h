#pragma once
#include <babylon_lite.h>
#include <filesystem>

struct Scene35Assets
{
    std::filesystem::path directory;
};
bl_IOService Scene35IO(Scene35Assets* assets);
