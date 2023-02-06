#include "Babylon/ShaderProcessor/ShaderCodeNode.h"
#include "Babylon/ShaderProcessor/ProcessingOptions.h"
#include "Babylon/ShaderProcessor/IShaderProcessor.h"

const char* defaultAttributeKeywordName = "attribute";
const char* defaultVaryingKeywordName = "varying";

bool Babylon::ShaderCodeNode::IsValid(std::map<std::string, std::string> preprocessors)
{
    return true;
}

int IndexOf(std::string str1, std::string value)
{
    std::string::size_type loc = str1.find(value, 0);
    
    if (loc == std::string::npos)
        return -1;

    return loc;
}

std::string Babylon::ShaderCodeNode::Process(std::map<std::string, std::string> preprocessors, ProcessingOptions& options)
{
    std::string result = "";

    if (!Line.empty())
    {
        auto value = Line;
        auto processor = options.Processor;

        if (processor)
        {
            // This must be done before other replacements to avoid mistakenly changing something that was already changed.
            if (processor->LineProcessor.has_value())
            {
                value = processor->LineProcessor.value()(value, options.IsFragment, options.ProcessingContext);
            }

            auto attributeKeyword = processor->AttributeKeywordName.has_value() ? processor->AttributeKeywordName.value() : defaultAttributeKeywordName;

            auto varyingKeyword =
                options.IsFragment && processor->VaryingFragmentKeywordName.has_value()
                ? options.Processor->VaryingFragmentKeywordName.value()
                : !options.IsFragment && processor->VaryingVertexKeywordName.has_value()
                ? processor->VaryingVertexKeywordName.value()
                : defaultVaryingKeywordName;

            if (!options.IsFragment && processor->AttributeProcessor.has_value() && Line.starts_with(attributeKeyword))
            {
                value = processor->AttributeProcessor.value()(Line, preprocessors, options.ProcessingContext);
            }
            else if (processor->VaryingProcessor.has_value() && Line.starts_with(varyingKeyword))
            {
                value = processor->VaryingProcessor.value()(Line, options.IsFragment, preprocessors, options.ProcessingContext);
            }
            else if (processor->UniformProcessor.has_value() && processor->UniformRegexp.has_value())
            {
                auto regex = processor->UniformRegexp.value();

                if (std::regex_match(Line, regex))
                {
                    if (!options.LookForClosingBracketForUniformBuffer)
                    {
                        value = processor->UniformProcessor.value()(Line, options.IsFragment, preprocessors, options.ProcessingContext);
                    }
                }
            }
            else if (processor->UniformBufferProcessor.has_value() && processor->UniformBufferRegexp.has_value())
            {
                auto regex = processor->UniformBufferRegexp.value();

                if (std::regex_match(Line, regex))
                {
                    if (!options.LookForClosingBracketForUniformBuffer)
                    {
                        value = processor->UniformBufferProcessor.value()(Line, options.IsFragment, options.ProcessingContext);
                        options.LookForClosingBracketForUniformBuffer = true;
                    }
                }
            }
            else if (processor->TextureProcessor.has_value() && processor->TextureRegexp)
            {
                auto regex = processor->TextureRegexp.value();

                if (std::regex_match(Line, regex))
                {
                    value = processor->TextureProcessor.value()(Line, options.IsFragment, preprocessors, options.ProcessingContext);
                }
            }
            else if ((processor->UniformProcessor.has_value() || processor->UniformBufferProcessor) && Line.starts_with("uniform") && !options.LookForClosingBracketForUniformBuffer)
            {
                auto regex = std::regex("uniform\\s+(?:(?:highp)?|(?:lowp)?)\\s*(\\S+)\\s+(\\S+)\\s*");

                if (std::regex_match(Line, regex))
                {
                    // uniform
                    if (processor->UniformProcessor)
                    {
                        value = processor->UniformProcessor.value()(Line, options.IsFragment, preprocessors, options.ProcessingContext);
                    }
                }
                else
                {
                    // Uniform buffer
                    if (processor->UniformBufferProcessor.has_value())
                    {
                        value = processor->UniformBufferProcessor.value()(Line, options.IsFragment, options.ProcessingContext);
                        options.LookForClosingBracketForUniformBuffer = true;
                    }
                }
            }

            if (options.LookForClosingBracketForUniformBuffer && IndexOf(Line, "}") != -1)
            {
                options.LookForClosingBracketForUniformBuffer = false;
                if (processor->EndOfUniformBufferProcessor.has_value())
                {
                    value = processor->EndOfUniformBufferProcessor.value()(Line, options.IsFragment, options.ProcessingContext);
                }
            }
        }
    }

    for (auto child : Children)
    {
        result += child->Process(preprocessors, options);
    }

    if (AdditionalDefineKey.has_value())
    {
        preprocessors[AdditionalDefineKey.value()] = AdditionalDefineValue || "true";
    }

    return result;
}
