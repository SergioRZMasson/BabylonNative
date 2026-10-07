#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Babylon::LiteShaderCompiler
{
    struct Attribute
    {
        std::string name;
        uint32_t location{};
        uint32_t semantic{};
        uint8_t components{};
    };

    struct UniformMember
    {
        std::string name;
        uint32_t byteOffset{};
        uint32_t byteSize{};
        uint32_t type{};
    };

    struct UniformBlock
    {
        std::string name;
        std::string uniformName;
        uint32_t group{};
        uint32_t binding{};
        uint32_t byteSize{};
        uint32_t byteOffset{};
        uint32_t registerCount{};
        std::vector<UniformMember> members;
    };

    struct TextureBinding
    {
        std::string name;
        std::string samplerName;
        std::string uniformName;
        uint32_t group{};
        uint32_t binding{};
        uint32_t samplerGroup{};
        uint32_t samplerBinding{};
        uint32_t stage{};
        uint32_t dimension{};
    };

    struct Stage
    {
        std::vector<uint8_t> bytes;
        std::vector<Attribute> attributes;
        std::vector<UniformBlock> blocks;
        std::vector<TextureBinding> textures;
        std::string diagnostics;
    };

    struct Program
    {
        Stage vertex;
        Stage fragment;
    };

    Program CompileD3D11(std::string_view vertexSource, std::string_view vertexEntry,
        std::string_view fragmentSource, std::string_view fragmentEntry);
}
