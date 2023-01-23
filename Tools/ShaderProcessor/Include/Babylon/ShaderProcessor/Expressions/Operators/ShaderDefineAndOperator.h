#pragma once

#include "Babylon/ShaderProcessor/Expressions/ShaderDefineExpression.h"

namespace Babylon
{
    class ShaderDefineAndOperator : public ShaderDefineExpression
    {
    public:
        ShaderDefineAndOperator() = default;
        ShaderDefineExpression* leftOperand;
        ShaderDefineExpression* rightOperand;

        bool isTrue(std::map<std::string, std::string> preprocessors) override
        {
            return leftOperand->isTrue(preprocessors) && rightOperand->isTrue(preprocessors);
        }
    };
}