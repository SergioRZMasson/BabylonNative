#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>

#include "Babylon/ShaderProcessor/ShaderCodeNode.h"
#include "Babylon/ShaderProcessor/Expressions/ShaderDefineExpression.h"

namespace Babylon
{
    class ShaderCodeTestNode : public ShaderCodeNode 
    {
    public:
        std::unique_ptr<ShaderDefineExpression> testExpression;

        bool IsValid(std::map<std::string, std::string> preprocessors) override
        {
            return testExpression->isTrue(preprocessors);
        }
    };
}