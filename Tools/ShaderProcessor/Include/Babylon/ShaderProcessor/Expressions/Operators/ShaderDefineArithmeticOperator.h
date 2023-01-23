#pragma once

#include "Babylon/ShaderProcessor/Expressions/ShaderDefineExpression.h"

namespace Babylon
{
    class ShaderDefineArithmeticOperator : public ShaderDefineExpression
    {
    public:
        ShaderDefineArithmeticOperator(std::string vDefine,
            std::string vOperand, std::string vTestValue)
            : define{vDefine}
            , operand{vOperand}
            , testValue{vTestValue}
        {
        }

        bool isTrue(std::map<std::string, std::string> preprocessors) override
        {
            auto value = define;

            if (preprocessors.find(define) != preprocessors.end())
            {
                auto value = preprocessors[define];
            }

            bool condition = false;
            int left = atoi(value.c_str());
            int right = atoi(testValue.c_str());

            if (operand == ">")
            {
                condition = left > right;
            }
            else if (operand == "<")
            {
                condition = left < right;
            }
            else if (operand == "<=")
            {
                condition = left <= right;
            }
            else if (operand == ">=")
            {
                condition = left >= right;
            }
            else if (operand == "==")
            {
                condition = left == right;
            }

            return condition;
        }

        std::string define;
        std::string operand;
        std::string testValue;

    };


}