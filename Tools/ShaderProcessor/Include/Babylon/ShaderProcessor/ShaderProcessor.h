#pragma once

#include "Babylon/ShaderProcessor/ShaderCodeNode.h"
#include "Babylon/ShaderProcessor/ShaderCodeCursor.h";
#include "Babylon/ShaderProcessor/ShaderCodeConditionNode.h";
#include "Babylon/ShaderProcessor/ShaderCodeTestNode.h";
#include "Babylon/ShaderProcessor/Expressions/Operators/shaderDefineIsDefinedOperator.h";
#include "Babylon/ShaderProcessor/Expressions/Operators/ShaderDefineOrOperator.h";
#include "Babylon/ShaderProcessor/Expressions/Operators/ShaderDefineAndOperator.h";
#include "Babylon/ShaderProcessor/Expressions/ShaderDefineExpression.h";
#include "Babylon/ShaderProcessor/Expressions/Operators/ShaderDefineArithmeticOperator.h";
#include "Babylon/ShaderProcessor/ProcessingOptions.h";
#include "Babylon/ShaderProcessor/IShaderProcessor.h"

#include <memory>
#include <functional>

namespace Babylon
{
    class ThinEngineWrapper;

    class ShaderProcessor
    {
        typedef std::function<void(std::string, std::string)> ShaderProcessorCallbackFunction;
        typedef std::function<void(std::string)> ProcessIncludesCallbackFunction;

    public:
        static void Initialize(ProcessingOptions& options);
        static void Process(std::string sourceCode, ProcessingOptions options, ShaderProcessorCallbackFunction& callback, ThinEngineWrapper& engine);
        static void PreProcess(std::string sourceCode, ProcessingOptions options, ShaderProcessorCallbackFunction& callback, ThinEngineWrapper& engine);
        static ShaderPair<std::string, std::string> Finalize(std::string vertexCode, std::string fragmentCode, ProcessingOptions options);
    
    private:
        static std::string _ProcessPrecision(std::string source, ProcessingOptions options);
        static std::unique_ptr<ShaderDefineExpression> _ExtractOperation(std::string expression);
        static std::unique_ptr<ShaderDefineExpression> _BuildSubExpression(std::string expression);
        static ShaderCodeTestNode* _BuildExpression(std::string line, int start);
        static void _MoveCursorWithinIf(ShaderCodeCursor* cursor, ShaderCodeConditionNode* rootNode, ShaderCodeNode* ifNode);
        static bool _MoveCursor(ShaderCodeCursor* cursor, ShaderCodeNode* rootNode);
        static std::string _EvaluatePreProcessors(std::string sourceCode, std::map<std::string, std::string> preprocessors, ProcessingOptions& options);
        static std::map<std::string, std::string> _PreparePreProcessors(ProcessingOptions& options, ThinEngineWrapper& engine);
        static std::string _ProcessShaderConversion(std::string sourceCode, ProcessingOptions options, ThinEngineWrapper& engine);
        static std::string _ApplyPreProcessing(std::string sourceCode, ProcessingOptions& options, ThinEngineWrapper& engine);
        static void _ProcessIncludes(std::string sourceCode, ProcessingOptions& options, ProcessIncludesCallbackFunction callback);
    };
}