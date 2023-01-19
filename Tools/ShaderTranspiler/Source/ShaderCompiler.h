#pragma once

#include <string_view>
#include <functional>
#include <spirv_cross.hpp>
#include <spirv_parser.hpp>

namespace bgfx
{
    struct Memory;
}

inline bool replaceInline(std::string& str, const std::string& from, const std::string& to)
{
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

namespace Babylon
{
    class ShaderProcessor final
    {
    public:
        ShaderProcessor(){};

        void attributeProcessor(std::string& attribute)
        {
            replaceInline(attribute, "attribute", "in");
        }


        void varyingProcessor(std::string& varying, bool isFragment)
        {
            replaceInline(varying, "varying", isFragment ? "in" : "out");
        }
    };

    /// This class is responsible for compiling the GLSL shader from Babylon.js into
    /// bgfx shader bytes with information about the shader attributes and uniforms.
    class ShaderTranspiler final
    {
    public:
        ShaderTranspiler();
        ~ShaderTranspiler();

        struct BgfxShaderInfo
        {
            std::vector<uint8_t> VertexBytes{};
            std::unordered_map<std::string, uint32_t> VertexAttributeLocations{};
            std::vector<uint8_t> FragmentBytes{};
            std::unordered_map<std::string, uint8_t> UniformStages{};
        };

        struct IncludesInfo
        {
            std::vector<std::string> IncludesNames;
            std::vector<std::string> IncludesData;
        };

        BgfxShaderInfo Compile(std::string_view vertexSource, std::string_view fragmentSource, std::string_view rootIncludePath);
    };
}
