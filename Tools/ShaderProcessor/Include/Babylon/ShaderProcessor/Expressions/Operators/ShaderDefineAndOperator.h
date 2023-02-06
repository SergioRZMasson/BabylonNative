#pragma once

#include "Babylon/ShaderProcessor/Expressions/ShaderDefineExpression.h"

namespace Babylon
{
    class ShaderDefineAndOperator : public ShaderDefineExpression
    {
    public:
        ShaderDefineAndOperator() = default;
        std::unique_ptr<ShaderDefineExpression> leftOperand;
        std::unique_ptr<ShaderDefineExpression> rightOperand;

        bool isTrue(std::map<std::string, std::string> preprocessors) override
        {
            return leftOperand->isTrue(preprocessors) && rightOperand->isTrue(preprocessors);
        }
    };
}