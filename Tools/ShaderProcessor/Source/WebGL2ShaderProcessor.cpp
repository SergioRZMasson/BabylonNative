#include "Babylon/ShaderProcessor/WebGL2ShaderProcessor.h"


static std::string attributeProcessor(std::string attribute, std::map<std::string, std::string>, Babylon::ShaderProcessingContext*)
{
    static const std::string attribute_str = "attribute";

    auto pos = attribute.find(attribute_str, 0);

    if (pos == std::string::npos)
        return attribute;

    return attribute.replace(pos, attribute_str.size(), "in");
}

static std::string varyingProcessor(std::string varying, bool isFragment, std::map<std::string, std::string>, Babylon::ShaderProcessingContext*)
{
    static const std::string varying_str = "varying";

    auto pos = varying.find(varying_str, 0);

    if (pos == std::string::npos)
        return varying;

    return varying.replace(pos, varying_str.size(), isFragment ? "in" : "out");
}

static std::vector<std::pair<std::regex, std::string>> g_postProcessingReplaces 
{
    // Remove extensions
    {std::regex{"#extension.+ (GL_OVR_multiview2 | GL_OES_standard_derivatives | GL_EXT_shader_texture_lod | GL_EXT_frag_depth | GL_EXT_draw_buffers).+ (enable | require)"}, ""},
    
    // Replace instructions
    {std::regex{"texture2D\\s*\\("}, "texture("}
}; 

static std::vector<std::pair<std::regex, std::string>> g_postProcessingFragReplaces
{
    {std::regex{"texture2DLodEXT\\s*\\("}, "textureLod("},
    {std::regex{"textureCube\\s*\\("}, "texture("},
    {std::regex{"gl_FragDepthEXT"}, "gl_FragDepth"},
    {std::regex{"gl_FragColor"}, "glFragColor"},
    {std::regex{"gl_FragData"}, "glFragData"},
};

static std::string postProcessor(std::string code, std::vector<std::string> defines, bool isFragment, Babylon::ShaderProcessingContext*)
{
    bool hasDrawBuffersExtension = std::regex_match(code, std::regex{"#extension.+GL_EXT_draw_buffers.+require"});

    for (auto const& pair : g_postProcessingReplaces)
    {
        code = std::regex_replace(code, pair.first, pair.second);
    }


    if (isFragment) 
    {
        for (auto const& pair : g_postProcessingFragReplaces)
        {
            code = std::regex_replace(code, pair.first, pair.second);
        }

        std::string cobineResult = (hasDrawBuffersExtension ? "" : "out vec4 glFragColor;\n");
        cobineResult = cobineResult + "void main(";
        code = std::regex_replace(code, std::regex{"void\\s+?main\\s*\\("}, cobineResult);
    } 
    else 
    {
        bool hasMultiviewExtension = std::find(defines.begin(), defines.end(), "#define MULTIVIEW") != defines.end();

        if (hasMultiviewExtension)
        {
            return "#extension GL_OVR_multiview2 : require\nlayout (num_views = 2) in;\n" + code;
        }
    }

    return code;
}

namespace Babylon
{
    WebGL2ShaderProcessor::WebGL2ShaderProcessor()
    {
        this->AttributeProcessor = &attributeProcessor;
        this->VaryingProcessor = &varyingProcessor;
        this->PostProcessor = &postProcessor;
    }
}