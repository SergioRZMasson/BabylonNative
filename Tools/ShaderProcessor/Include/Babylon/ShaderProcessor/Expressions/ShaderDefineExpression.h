#pragma once

#include <map>
#include <vector>
#include <string>

namespace Babylon
{
	class ShaderDefineExpression
    {
    public:
        virtual bool isTrue(std::map<std::string, std::string> preprocessors) { return true; }
        static std::string postfixToInfix(std::vector<std::string> postfix);
        static std::vector<std::string> infixToPostfix(std::string infix);
    };
}