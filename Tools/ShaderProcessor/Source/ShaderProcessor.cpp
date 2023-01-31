#include "Babylon/ShaderProcessor/ShaderProcessor.h"
#include "Babylon/ShaderProcessor/IShaderProcessor.h"
#include "Babylon/ShaderProcessor/StringUtils.h"

namespace Babylon
{
    class ThinEngineWrapper
    {
    };

    std::regex regexSE = std::regex("defined\\s*?\\((.+?)\\)");
    std::regex regexSERevert = std::regex("defined\\s*?\\[(.+?)\\]");
    std::regex regexShaderInclude = std::regex("#include\s?<(.+)>(\((.*)\))*(\[(.*)\])*");

    void ShaderProcessor::Initialize(ProcessingOptions& options)
    {
        if (options.Processor && options.Processor->InitializeShaders.has_value())
        {
            options.Processor->InitializeShaders.value()(options.ProcessingContext);
        }
    }

    void ShaderProcessor::Process(std::string sourceCode, ProcessingOptions options, ShaderProcessorCallbackFunction& callback, ThinEngineWrapper& engine)
    {
        if (options.Processor && options.Processor->PreProcessShaderCode.has_value())
        {
            sourceCode = options.Processor->PreProcessShaderCode.value()(sourceCode, options.IsFragment);
        }

        _ProcessIncludes(sourceCode, options, [&](std::string codeWithIncludes)
        {
            if (options.ProcessCodeAfterIncludes)
            {
                codeWithIncludes = options.ProcessCodeAfterIncludes(options.IsFragment ? "fragment" : "vertex", codeWithIncludes);
            }

            auto migratedCode = _ProcessShaderConversion(codeWithIncludes, options, engine);
            
            callback(migratedCode, codeWithIncludes);
        });
    }

    void ShaderProcessor::PreProcess(std::string sourceCode, ProcessingOptions options, ShaderProcessorCallbackFunction& callback, ThinEngineWrapper& engine)
    {
        if (options.Processor && options.Processor->PreProcessShaderCode.has_value())
        {
            sourceCode = options.Processor->PreProcessShaderCode.value()(sourceCode, options.IsFragment);
        }

        _ProcessIncludes(sourceCode, options, [&](std::string codeWithIncludes)
        {
            if (options.ProcessCodeAfterIncludes)
            {
                codeWithIncludes = options.ProcessCodeAfterIncludes(options.IsFragment ? "fragment" : "vertex", codeWithIncludes);
            }
            auto migratedCode = _ApplyPreProcessing(codeWithIncludes, options, engine);
            callback(migratedCode, codeWithIncludes);
        });
    }

    ShaderPair<std::string, std::string> ShaderProcessor::Finalize(std::string vertexCode, std::string fragmentCode, ProcessingOptions options)
    {
        if (!options.Processor || !options.Processor->FinalizeShaders.has_value())
        {
            return {vertexCode, fragmentCode};
        }

        return options.Processor->FinalizeShaders.value()(vertexCode, fragmentCode, options.ProcessingContext);
    }

    std::string ShaderProcessor::_ProcessPrecision(std::string source, ProcessingOptions options)
    {
        if (options.Processor && options.Processor->NoPrecision.has_value())
        {
            return source;
        }

        auto shouldUseHighPrecisionShader = options.ShouldUseHighPrecisionShader;

        if (source.find("precision highp float") == std::string::npos)
        {
            if (!shouldUseHighPrecisionShader)
            {
                source = "precision mediump float;\n" + source;
            }
            else
            {
                source = "precision highp float;\n" + source;
            }
        }
        else
        {
            if (!shouldUseHighPrecisionShader)
            {
                // Moving highp to mediump
                source = String::Replace(source, "precision highp float", "precision mediump float");
            }
        }

        return source;
    }

    std::unique_ptr<ShaderDefineExpression> ShaderProcessor::_ExtractOperation(std::string expression)
    {
        auto regex = std::regex("defined \\((.+)\\)");

        std::smatch base_match;

        auto match = std::regex_match(expression, base_match, regex);

        if (match)
        {
            return std::make_unique<ShaderDefineIsDefinedOperator>(String::Trim(base_match[1]), expression[0] == '!');
        }

        std::vector<std::string> operators = {"==", ">=", "<=", "<", ">"};
        std::string operator_ = "";
        auto indexOperator = 0;

        for (size_t i = 0; i < operators.size(); i++)
        {
            operator_ = operators[i];

            indexOperator = expression.find(operator_);

            if (indexOperator != std::string::npos)
            {
                break;
            }
        }

        if (indexOperator == std::string::npos)
        {
            return std::make_unique<ShaderDefineIsDefinedOperator>(expression);
        }


        auto define = String::Trim(expression.substr(0, indexOperator));
        auto value = String::Trim(expression.substr(indexOperator + operator_.size()));

        return std::make_unique<ShaderDefineArithmeticOperator>(define, operator_, value);
    }

    std::unique_ptr<ShaderDefineExpression> ShaderProcessor::_BuildSubExpression(std::string expression)
    {
        expression = expression.replace(regexSE, "defined[$1]");

        const postfix = ShaderDefineExpression.infixToPostfix(expression);

        const stack : (string | ShaderDefineExpression)[] = [];

        for (const c of postfix)
        {
            if (c != = "||" && c != = "&&")
            {
                stack.push(c);
            }
            else if (stack.length >= 2)
            {
                let v1 = stack[stack.length - 1],
                    v2 = stack[stack.length - 2];

                stack.length -= 2;

                const operator= c == "&&" ? new ShaderDefineAndOperator() : new ShaderDefineOrOperator();

                if (typeof v1 == = "string")
                {
                    v1 = v1.replace(regexSERevert, "defined($1)");
                }

                if (typeof v2 == = "string")
                {
                    v2 = v2.replace(regexSERevert, "defined($1)");
                }

                operator.leftOperand = typeof v2 == = "string" ? this._ExtractOperation(v2) : v2;
                operator.rightOperand = typeof v1 == = "string" ? this._ExtractOperation(v1) : v1;

                stack.push(operator);
            }
        }

        let result = stack[stack.length - 1];

        if (typeof result == = "string")
        {
            result = result.replace(regexSERevert, "defined($1)");
        }

        // note: stack.length !== 1 if there was an error in the parsing

        return typeof result == = "string" ? this._ExtractOperation(result) : result;
    }

    std::unique_ptr<ShaderCodeTestNode> ShaderProcessor::_BuildExpression(std::string line, int start)
    {
        return std::unique_ptr<ShaderCodeTestNode>();
    }

    void ShaderProcessor::_MoveCursorWithinIf(ShaderCodeCursor& cursor, ShaderCodeConditionNode& rootNode, ShaderCodeNode& ifNode)
    {
    }

    bool ShaderProcessor::_MoveCursor(ShaderCodeCursor& cursor, ShaderCodeNode& rootNode)
    {
        return false;
    }

    std::string ShaderProcessor::_EvaluatePreProcessors(std::string sourceCode, std::map<std::string, std::string> preprocessors, ProcessingOptions& options)
    {
        return std::string();
    }

    std::map<std::string, std::string> ShaderProcessor::_PreparePreProcessors(ProcessingOptions& options, ThinEngineWrapper& engine)
    {
        return std::map<std::string, std::string>();
    }

    std::string ShaderProcessor::_ProcessShaderConversion(std::string sourceCode, ProcessingOptions options, ThinEngineWrapper& engine)
    {
        return std::string();
    }

    std::string ShaderProcessor::_ApplyPreProcessing(std::string sourceCode, ProcessingOptions& options, ThinEngineWrapper& engine)
    {
        return std::string();
    }

    void ShaderProcessor::_ProcessIncludes(std::string sourceCode, ProcessingOptions& options, ProcessIncludesCallbackFunction callback)
    {
    }
}