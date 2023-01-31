#pragma once

#include <string>
#include <regex>
#include <map>
#include <vector>
#include <optional>
#include <functional>

namespace Babylon
{
    typedef void ShaderProcessingContext;

    template<typename var1, typename var2>
    struct ShaderPair
    {
        var1 vertexCode;
        var2 fragmentCode;
    };

    typedef std::optional<std::function<std::string(std::string, bool)>> PreProcessShaderCodeFunction;
    typedef std::optional<std::function<std::string(std::string attribute, std::map<std::string, std::string> preProcessors, ShaderProcessingContext* processingContext)>> AttributeProcessorFunction;
    typedef std::optional<std::function<std::string(std::string, bool isFragment, std::map<std::string, std::string> preProcessors, ShaderProcessingContext* processingContext)>> VaryingProcessorFunction;
    typedef std::optional<std::function<std::string(std::string uniform, bool isFragment, std::map<std::string, std::string> preProcessors, ShaderProcessingContext* processingContext)>> UniformProcessorFunction;
    typedef std::optional<std::function<std::string(std::string uniformBuffer, bool isFragment, ShaderProcessingContext* processingContext)>> UniformBufferProcessorFunction;
    typedef std::optional<std::function<std::string(std::string texture, bool isFragment, std::map<std::string, std::string> preProcessors, ShaderProcessingContext* processingContext)>> TextureProcessorFunction;
    typedef std::optional<std::function<std::string(std::string closingBracketLine, bool isFragment, ShaderProcessingContext* processingContext)>> EndOfUniformBufferProcessorFunction;
    typedef std::optional<std::function<std::string(std::string line, bool isFragment, ShaderProcessingContext* processingContext)>> LineProcessorFunction;
    typedef std::optional<std::function<std::string(std::string code, std::vector<std::string> defines, bool isFragment, ShaderProcessingContext* processingContext)>> PreProcessorFunction;
    typedef std::optional<std::function<std::string(std::string code, std::vector<std::string> defines, bool isFragment, ShaderProcessingContext* processingContext)>> PostProcessorFunction;
    typedef std::optional<std::function<std::string(ShaderProcessingContext* processingContext)>> InitializeShadersFunction;
    typedef std::optional<std::function<ShaderPair<std::string, std::string>(std::string vertexCode, std::string fragmentCode, ShaderProcessingContext* processingContext)>> FinalizeShadersFunction;

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