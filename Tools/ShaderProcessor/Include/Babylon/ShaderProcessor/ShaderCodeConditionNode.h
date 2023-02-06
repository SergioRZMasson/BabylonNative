#pragma once

#include "Babylon/ShaderProcessor/ShaderCodeNode.h"

namespace Babylon
{
    class ShaderCodeConditionNode : public ShaderCodeNode
    {
    public:
        ShaderCodeConditionNode() = default;

        std::string Process( std::map<std::string, std::string> preprocessors, ProcessingOptions& options) override
        {
            for (int index = 0; index < Children.size() ; index++)
            {
                auto node = Children[index];

                if (node->IsValid(preprocessors))
                {
                    return node->Process(preprocessors, options);
                }
            }

            return "";
        }

    };
}