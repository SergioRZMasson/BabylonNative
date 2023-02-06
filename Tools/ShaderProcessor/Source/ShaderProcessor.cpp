#include "Babylon/ShaderProcessor/ShaderProcessor.h"
#include "Babylon/ShaderProcessor/IShaderProcessor.h"
#include "Babylon/ShaderProcessor/StringUtils.h"

#include<stack>

namespace Babylon
{
    struct EngineFeatures
    {
        bool needShaderCodeInlining{false};
    };

    class ThinEngineWrapper
    {
    public:
        void _getGlobalDefines(std::map<std::string, std::string>& defines)
        {

        }

        std::string inlineShaderCode(std::string& code) 
        {
            return nullptr;
        }

        EngineFeatures _features;
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
                source = String::ReplaceInplace(source, "precision highp float", "precision mediump float");
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

    struct StringOrExpression
    {
        std::optional<std::string> string;
        std::unique_ptr<ShaderDefineExpression> expression;
    };

    std::unique_ptr<ShaderDefineExpression> ShaderProcessor::_BuildSubExpression(std::string expression)
    {
        expression = std::regex_replace(expression, regexSE, "defined[$1]");

        auto postfix = ShaderDefineExpression::infixToPostfix(expression);

        std::stack<StringOrExpression> stack{};

        for (auto& c : postfix)
        {
            if (c != "||" && c != "&&")
            {
                stack.push({c, nullptr});
            }
            else if (stack.size() >= 2)
            {
                auto v1 = std::move(stack.top());
                stack.pop();
                auto v2 = std::move(stack.top());
                stack.pop();

                if (v1.string.has_value())
                {
                    v1.string = std::regex_replace(v1.string.value(), regexSERevert, "defined($1)");
                }

                if (v2.string.has_value())
                {
                    v2.string = std::regex_replace(v2.string.value(), regexSERevert, "defined($1)");
                }

                if (c == "&&")
                {
                    ShaderDefineAndOperator* _operator = new ShaderDefineAndOperator();
                    _operator->leftOperand = v2.string.has_value() ? _ExtractOperation(v2.string.value()) : std::move(v2.expression);
                    _operator->rightOperand = v1.string.has_value() ? _ExtractOperation(v1.string.value()) : std::move(v1.expression);
                    
                    stack.push({nullptr, std::make_unique<ShaderDefineExpression>(_operator)});
                }
                else
                {
                    ShaderDefineOrOperator* _operator = new ShaderDefineOrOperator();
                    _operator->leftOperand = v2.string.has_value() ? _ExtractOperation(v2.string.value()) : std::move(v2.expression);
                    _operator->rightOperand = v1.string.has_value() ? _ExtractOperation(v1.string.value()) : std::move(v1.expression);
                    
                    stack.push({nullptr, std::make_unique<ShaderDefineExpression>(_operator)});
                }
            }
        }

        auto& result = stack.top();

        if (result.string.has_value())
        {
            result.string = std::regex_replace(result.string.value(), regexSERevert, "defined($1)");
        }

        // note: stack.length !== 1 if there was an error in the parsing

        return result.string.has_value() ? _ExtractOperation(result.string.value()) : std::move(result.expression);
    }

    ShaderCodeTestNode* ShaderProcessor::_BuildExpression(std::string line, int start)
    {
        auto node = new ShaderCodeTestNode();
        auto command = line.substr(0, start);
        auto expression = line.substr(start);

        expression = String::Trim(expression.substr(0, (expression.find("//") + 1 || expression.size() + 1) - 1));

        if (command == "#ifdef")
        {
            node->testExpression = std::make_unique<ShaderDefineIsDefinedOperator>(expression);
        }
        else if (command == "#ifndef")
        {
            node->testExpression = std::make_unique<ShaderDefineIsDefinedOperator>(expression, true);
        }
        else
        {
            node->testExpression = _BuildSubExpression(expression);
        }

        return node;
    }

    void ShaderProcessor::_MoveCursorWithinIf(ShaderCodeCursor* cursor, ShaderCodeConditionNode* rootNode, ShaderCodeNode* ifNode)
    {
        auto line = cursor->currentLine();

        while (_MoveCursor(cursor, ifNode))
        {
            line = cursor->currentLine();
            ;
            auto first5 = String::ToLower(line.substr(0, 5));

            if (first5 == "#else")
            {
                auto elseNode = std::make_shared<ShaderCodeNode>();
                rootNode->Children.push_back(elseNode);
                _MoveCursor(cursor, elseNode.get());
                return;
            }
            else if (first5 == "#elif")
            {
                auto elifNode = _BuildExpression(line, 5);
                rootNode->Children.push_back(std::make_shared<ShaderCodeNode>(elifNode));
                *ifNode = *elifNode;
            }
        }
    }

    bool ShaderProcessor::_MoveCursor(ShaderCodeCursor* cursor, ShaderCodeNode* rootNode)
    {
        while (cursor->canRead())
        {
            cursor->lineIndex++;
            auto line = cursor->currentLine();
            std::regex keywords = std::regex{"(#ifdef)|(#else)|(#elif)|(#endif)|(#ifndef)|(#if)"};
            
            std::smatch matches;

            std::regex_match(line, matches, keywords);

            if (matches.size() != 0)
            {
                std::string keyword = matches[0].str();

                if (keyword == "#ifdef")
                {
                    auto newRootNode = std::make_shared<ShaderCodeConditionNode>();
                    rootNode->Children.push_back(newRootNode);

                    auto ifNode = _BuildExpression(line, 6);
                    newRootNode->Children.push_back(std::make_shared<ShaderCodeNode>(ifNode));
                    _MoveCursorWithinIf(cursor, newRootNode.get(), ifNode);
                    break;
                }
                else if (keyword == "#else" || keyword == "#elif")
                {
                    return true;
                }
                else if (keyword == "#endif")
                {
                    return false;
                }
                else if (keyword == "#ifndef")
                {
                    auto newRootNode = std::make_shared<ShaderCodeConditionNode>();
                    rootNode->Children.push_back(newRootNode);

                    auto ifNode = _BuildExpression(line, 7);
                    newRootNode->Children.push_back(std::make_shared<ShaderCodeNode>(ifNode));
                    _MoveCursorWithinIf(cursor, newRootNode.get(), ifNode);
                    break;
                }
                else if (keyword == "#if")
                {
                    auto newRootNode = std::make_shared<ShaderCodeConditionNode>();
                    auto ifNode = _BuildExpression(line, 3);
                    rootNode->Children.push_back(newRootNode);

                    newRootNode->Children.push_back(std::make_shared<ShaderCodeNode>(ifNode));
                    _MoveCursorWithinIf(cursor, newRootNode.get(), ifNode);
                    break;
                }
            }
            else
            {
                auto newNode = std::make_shared<ShaderCodeNode>();
                newNode->Line = line;
                rootNode->Children.push_back(newNode);

                // Detect additional defines
                if (line[0] == '#' && line[1] == 'd')
                {
                    auto split = String::Split(String::Replace(line, ";", ""), " ");
                    newNode->AdditionalDefineKey = split[1];

                    if (split.size() == 3)
                    {
                        newNode->AdditionalDefineValue = split[2];
                    }
                }
            }
        }

        return false;
    }

    std::string ShaderProcessor::_EvaluatePreProcessors(std::string sourceCode, std::map<std::string, std::string> preprocessors, ProcessingOptions& options)
    {
        auto rootNode = ShaderCodeNode();
        auto cursor = ShaderCodeCursor();

        cursor.lineIndex = -1;
        cursor.SetLines(String::Split(sourceCode, "\n"));

        // Decompose (We keep it in 2 steps so it is easier to maintain and perf hit is insignificant)
        _MoveCursor(&cursor, &rootNode);

        // Recompose
        return rootNode.Process(preprocessors, options);
    }

    std::map<std::string, std::string> ShaderProcessor::_PreparePreProcessors(ProcessingOptions& options, ThinEngineWrapper& engine)
    {
        auto defines = options.Defines;

        std::map<std::string, std::string> preprocessors{};

        for (auto& define : defines)
        {
            auto keyValue = String::Trim(String::Replace(String::Replace(define, "#define", ""), ";", ""));
            auto split = String::Split(keyValue , " ");
            preprocessors[split[0]] = split.size() > 1 ? split[1] : "";
        }

        preprocessors["GL_ES"] = "true";
        preprocessors["__VERSION__"] = options.Version;
        preprocessors[options.PlatformName] = "true";

        engine._getGlobalDefines(preprocessors);

        return preprocessors;
    }

    std::string ShaderProcessor::_ProcessShaderConversion(std::string sourceCode, ProcessingOptions options, ThinEngineWrapper& engine)
    {
        auto preparedSourceCode = _ProcessPrecision(sourceCode, options);

        if (!options.Processor)
        {
            return preparedSourceCode;
        }

        // Already converted
        if (true && preparedSourceCode.find("#version 3") != std::string::npos)
        {
            preparedSourceCode = String::Replace(preparedSourceCode, "#version 300 es", "");
            
            if (!options.Processor->ParseGLES3)
            {
                return preparedSourceCode;
            }
        }

        auto defines = options.Defines;
        auto preprocessors = _PreparePreProcessors(options, engine);

        // General pre processing
        if (options.Processor->PreProcessor.has_value())
        {
            preparedSourceCode = options.Processor->PreProcessor.value()(preparedSourceCode, defines, options.IsFragment, options.ProcessingContext);
        }

        preparedSourceCode = _EvaluatePreProcessors(preparedSourceCode, preprocessors, options);

        // Post processing
        if (options.Processor->PostProcessor.has_value())
        {
            preparedSourceCode = options.Processor->PostProcessor.value()(preparedSourceCode, defines, options.IsFragment, options.ProcessingContext);
        }

        // Inline functions tagged with #define inline
        if (engine._features.needShaderCodeInlining)
        {
            preparedSourceCode = engine.inlineShaderCode(preparedSourceCode);
        }

        return preparedSourceCode;
    }

    std::string ShaderProcessor::_ApplyPreProcessing(std::string sourceCode, ProcessingOptions& options, ThinEngineWrapper& engine)
    {
        auto preparedSourceCode = sourceCode;

        auto defines = options.Defines;

        auto preprocessors = _PreparePreProcessors(options, engine);

        // General pre processing
        if (options.Processor && options.Processor->PreProcessor.has_value())
        {
            preparedSourceCode = options.Processor->PreProcessor.value()(preparedSourceCode, defines, options.IsFragment, options.ProcessingContext);
        }

        preparedSourceCode = _EvaluatePreProcessors(preparedSourceCode, preprocessors, options);

        // Post processing
        if (options.Processor && options.Processor->PostProcessor)
        {
            preparedSourceCode = options.Processor->PostProcessor.value()(preparedSourceCode, defines, options.IsFragment, options.ProcessingContext);
        }

        // Inline functions tagged with #define inline
        if (engine._features.needShaderCodeInlining)
        {
            preparedSourceCode = engine.inlineShaderCode(preparedSourceCode);
        }

        return preparedSourceCode;
    }

    void ShaderProcessor::_ProcessIncludes(std::string sourceCode, ProcessingOptions& options, ProcessIncludesCallbackFunction callback)
    {
        std::smatch match;

        std::regex_match(sourceCode, match, regexShaderInclude);

        auto returnValue = sourceCode;
        auto keepProcessing = false;

        while (match.size() != 0)
        {
            auto includeFile = match[1].str();

            // Uniform declaration
            if (includeFile.find("__decl__") != std::string::npos)
            {
                includeFile = std::regex_replace(includeFile, std::regex{"__decl__"}, "");

                if (options.SupportsUniformBuffers)
                {
                    includeFile = std::regex_replace(includeFile, std::regex{"Vertex"}, "Ubo");
                    includeFile = std::regex_replace(includeFile, std::regex{"Fragment"}, "Ubo");
                }

                includeFile = includeFile + "Declaration";
            }

            if (options.IncludesShadersStore.find(includeFile) != options.IncludesShadersStore.end())
            {
                // Substitution
                auto includeContent = options.IncludesShadersStore[includeFile];

                if (match.size() >= 3)
                {
                    auto splits = String::Split(match[3].str(), ",");

                    for (int index = 0; index < splits.size(); index += 2)
                    {
                        auto source = std::regex{splits[index]};
                        auto dest = splits[index + 1];

                        includeContent = std::regex_replace(includeContent, source, dest);
                    }
                }

                if (match.size() >= 5)
                {
                    auto indexString = match[5].str();

                    if (indexString.find("..") != std::string::npos)
                    {
                        auto indexSplits = String::Split(indexString, "..");

                        std::optional<int> minIndex{};
                        
                        if (String::IsNumber(indexSplits[0]))
                        {
                            minIndex = std::atoi(indexSplits[0].c_str());
                        }
                        
                        std::optional<int> maxIndex;
                        
                        if (String::IsNumber(indexSplits[1]))
                        {
                            maxIndex = std::atoi(indexSplits[1].c_str());
                        }
                        

                        auto sourceIncludeContent = includeContent.substr(0);
                        includeContent = "";

                        if (!maxIndex.has_value())
                        {
                            if (options.IndexParameters.find(indexSplits[1]) != options.IndexParameters.end())
                            {
                                maxIndex = std::atoi(options.IndexParameters[indexSplits[1]].c_str());
                            }
                            else
                            {
                                maxIndex = 0;
                            }
                        }

                        for (int i = minIndex.value(); i < maxIndex.value(); i++)
                        {
                            if (!options.SupportsUniformBuffers)
                            {
                                // Ubo replacement
                                sourceIncludeContent = sourceIncludeContent.replace(
                                    / light\{X\}.(\w*) / g, (str
                                                                : string, p1
                                                                : string) = > {
                                          return p1 + "{X}";
                                      });
                            }
                            includeContent += sourceIncludeContent.replace(/\{X\} / g, i.toString()) + "\n";
                        }
                    }
                    else
                    {
                        if (!options.supportsUniformBuffers)
                        {
                            // Ubo replacement
                            includeContent = includeContent.replace(
                                / light\{X\}.(\w*) / g, (str
                                                            : string, p1
                                                            : string) = > {
                                      return p1 + "{X}";
                                  });
                        }
                        includeContent = includeContent.replace(/\{X\} / g, indexString);
                    }
                }

                // Replace
                returnValue = returnValue.replace(match[0], includeContent);

                keepProcessing = keepProcessing || includeContent.indexOf("#include<") >= 0 || includeContent.indexOf("#include <") >= 0;
            }
            else
            {
                const includeShaderUrl = options.shadersRepository + "ShadersInclude/" + includeFile + ".fx";

                ShaderProcessor._FileToolsLoadFile(
                    includeShaderUrl, (fileContent) = >
                        {
                            options.includesShadersStore[includeFile] = fileContent as string;
                            this._ProcessIncludes(<string> returnValue, options, callback);
                        });
                return;
            }

            match = regexShaderInclude.exec(sourceCode);
        }

        if (keepProcessing)
        {
            _ProcessIncludes(returnValue, options, callback);
        }
        else
        {
            callback(returnValue);
        }
    }
}