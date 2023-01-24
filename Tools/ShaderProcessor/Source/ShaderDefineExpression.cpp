#include "Babylon/ShaderProcessor/Expressions/ShaderDefineExpression.h"
#include "Babylon/ShaderProcessor/StringUtils.h"

#include <map>
#include <vector>
#include <stack>
#include <string>
#include <list>
#include <sstream>
#include <stdarg.h>
#include <regex>

std::vector<std::string> _Stack{"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""};

std::map<std::string, int> _OperatorPriority{
    {")", 0},
    {"(", 1},
    {"||", 2},
    {"&&", 3}};

std::string Babylon::ShaderDefineExpression::postfixToInfix(std::vector<std::string> postfix)
{
    std::list<std::string> stack{};

    for (auto& c : postfix)
    {
        if (_OperatorPriority.find(c) == _OperatorPriority.end())
        {
            stack.push_front(c);
        }
        else
        {
            std::string v1 = stack.back();
            stack.pop_back();
            std::string v2 = stack.back();
            stack.pop_back();
            stack.push_front(String::Format("(%s%s%s)", v2.c_str(), c.c_str(), v1.c_str()));
        }
    }

    return stack.back();
}

std::vector<std::string> Babylon::ShaderDefineExpression::infixToPostfix(std::string infix)
{
    std::vector<std::string> result{};

    int stackIdx = -1;

    int idx = 0;
    std::string operand = "";

    auto pushOperand = [&]()
    {
        operand = String::Trim(operand);

        if (operand != "")
        {
            result.push_back(operand);
            operand = "";
        }
    };

    auto push = [&](std::string s)
    {
        if (stackIdx < _Stack.size() - 1)
        {
            _Stack[++stackIdx] = s;
        }
    };

    auto peek = [&](){ return _Stack[stackIdx]; };

    auto pop = [&]()
    { 
        return (stackIdx == -1 ? "!!INVALID EXPRESSION!!" : _Stack[stackIdx--]); 
    };

    while (idx < infix.size())
    {
        auto c = infix[idx];
        auto token = idx < infix.size() - 1 ? infix.substr(idx, 2) : "";

        if (c == '(')
        {
            operand = "";
            push(std::string{c});
        }
        else if (c == ')')
        {
            pushOperand();
            while (stackIdx != -1 && peek() != "(")
            {
                result.push_back(pop());
            }
            pop();
        }
        else if (_OperatorPriority[token] > 1)
        {
            pushOperand();
            while (stackIdx != -1 && _OperatorPriority[peek()] >= _OperatorPriority[token])
            {
                result.push_back(pop());
            }
            push(token);
            idx++;
        }
        else
        {
            operand += c;
        }
        idx++;
    }

    pushOperand();

    while (stackIdx != -1)
    {
        if (peek() == "(")
        {
            pop();
        }
        else
        {
            result.push_back(pop());
        }
    }

    return result;
}