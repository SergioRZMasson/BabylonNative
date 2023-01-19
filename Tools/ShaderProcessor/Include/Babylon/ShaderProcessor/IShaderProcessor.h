#pragma once

#include <string>
#include <regex>
#include <map>
#include <vector>
#include <optional>

namespace Babylon
{
    typedef void ShaderProcessingContext;

    typedef std::string (*PreProcessShaderCodeFunction)(std::string, bool);
    typedef std::string (*AttributeProcessorFunction)(std::string attribute, std::map<std::string, std::string> preProcessors, ShaderProcessingContext* processingContext);
    typedef std::string (*VaryingProcessorFunction)(std::string varying, bool isFragment, std::map<std::string, std::string> preProcessors, ShaderProcessingContext* processingContext);
    typedef std::string (*UniformProcessorFunction)(std::string uniform, bool isFragment, std::map<std::string, std::string> preProcessors, ShaderProcessingContext* processingContext);
    typedef std::string (*UniformBufferProcessorFunction)(std::string uniformBuffer, bool isFragment, ShaderProcessingContext* processingContext);
    typedef std::string (*TextureProcessorFunction)(std::string texture, bool isFragment, std::map<std::string, std::string> preProcessors, ShaderProcessingContext* processingContext);
    typedef std::string (*EndOfUniformBufferProcessorFunction)(std::string closingBracketLine, bool isFragment, ShaderProcessingContext* processingContext);
    typedef std::string (*LineProcessorFunction)(std::string line, bool isFragment, ShaderProcessingContext* processingContext);
    typedef std::string (*PreProcessorFunction)(std::string code, std::vector<std::string> defines, bool isFragment, ShaderProcessingContext* processingContext);
    typedef std::string (*PostProcessorFunction)(std::string code, std::vector<std::string> defines, bool isFragment, ShaderProcessingContext* processingContext);
    typedef void (*InitializeShadersFunction)(ShaderProcessingContext* processingContext);
    typedef void (*FinalizeShadersFunction)(std::string vertexCode, std::string fragmentCode, ShaderProcessingContext* processingContext);

    class IShaderProcessor
    {
    public:
        PreProcessShaderCodeFunction PreProcessShaderCode;
        AttributeProcessorFunction AttributeProcessor;
        VaryingProcessorFunction VaryingProcessor;
        UniformProcessorFunction UniformProcessor;
        UniformBufferProcessorFunction UniformBufferProcessor;
        TextureProcessorFunction TextureProcessor;
        EndOfUniformBufferProcessorFunction EndOfUniformBufferProcessor;
        LineProcessorFunction LineProcessor;
        PreProcessorFunction PreProcessor;
        PostProcessorFunction PostProcessor;
        InitializeShadersFunction InitializeShaders;
        FinalizeShadersFunction FinalizeShaders;

        std::optional<std::string> AttributeKeywordName;
        std::optional<std::string> VaryingVertexKeywordName;
        std::optional<std::string> VaryingFragmentKeywordName;

        std::optional<std::regex> UniformRegexp;
        std::optional<std::regex> UniformBufferRegexp;
        std::optional<std::regex> TextureRegexp;
            
        std::optional<bool> NoPrecision;
        std::optional<bool> ParseGLES3;
    };
}