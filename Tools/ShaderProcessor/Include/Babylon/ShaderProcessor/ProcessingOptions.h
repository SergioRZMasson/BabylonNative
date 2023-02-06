#pragma once

#include <string>
#include <vector>
#include <map>

namespace Babylon 
{
    typedef std::string (*ShaderCustomProcessingFunction)(std::string, std::string);

    class ShaderProcessingContext;
    class IShaderProcessor;

    struct ProcessingOptions
    {
        std::vector<std::string> Defines;
        std::map<std::string, std::string> IndexParameters;
        bool IsFragment;
        bool ShouldUseHighPrecisionShader;
        bool SupportsUniformBuffers;
        std::string ShadersRepository;
        std::map<std::string, std::string> IncludesShadersStore;
        IShaderProcessor* Processor;
        std::string Version;
        std::string PlatformName;
        bool LookForClosingBracketForUniformBuffer;
        ShaderProcessingContext* ProcessingContext;
        bool IsNDCHalfZRange;
        bool UseReverseDepthBuffer;
        ShaderCustomProcessingFunction ProcessCodeAfterIncludes;
    };
}