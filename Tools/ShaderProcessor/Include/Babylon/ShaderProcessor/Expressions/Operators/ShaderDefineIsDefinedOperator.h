#pragma once 

#include "Babylon/ShaderProcessor/Expressions/ShaderDefineExpression.h"

namespace Babylon
{
    class ShaderDefineIsDefinedOperator : public ShaderDefineExpression
    {
    public:
        ShaderDefineIsDefinedOperator(std::string define, bool notV)
            : m_define{define}
            , m_not{notV}
        {
        }

        bool isTrue(std::map<std::string, std::string> preprocessors) override 
        { 
            auto condition = preprocessors.find(m_define) != preprocessors.end();

            if (m_not)
            {
                condition = !condition;
            }

            return condition;
        }

    private:
        std::string m_define;
        bool m_not;

    };
}