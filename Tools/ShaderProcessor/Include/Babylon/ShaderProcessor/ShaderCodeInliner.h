#pragma once

#include <string>
#include <vector>

namespace Babylon
{
    struct IInlineFunctionDescr
    {
        std::string name;
        std::string type;
        std::vector<std::string> parameters;
        std::string body;
        int callIndex;
    };

    class ShaderCodeInliner
    {
    public:
        ShaderCodeInliner(std::string sourceCode, int numMaxIterations = 20)
        {
            _sourceCode = sourceCode;
            _numMaxIterations = numMaxIterations;
            _functionDescr = {};
            inlineToken = "#define inline";
        }

        /** Gets the code after the inlining process */
        std::string GetCode() const { return _sourceCode; }

        void processCode();

        std::string inlineToken;
        bool debug{false};

    private:
        std::string _sourceCode;
        std::vector<IInlineFunctionDescr> _functionDescr;
        int _numMaxIterations;

        void _collectFunctions();
        bool _processInlining(int numMaxIterations = 20);
        bool _replaceFunctionCallsByCode();
        std::string _replaceNames(std::string code, std::vector<std::string> sources, std::vector<std::string> destinations);
    };
}