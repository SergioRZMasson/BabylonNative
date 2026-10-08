#pragma once

#include <string>
#include <vector>

namespace LitePlayground::UiCss
{
    struct Declaration
    {
        std::string name;
        std::string value;
    };

    struct OpacityTransition
    {
        bool initialized{};
        bool pending{};
        bool active{};
        double value{1};
        double from{1};
        double end{1};
        double reversingStart{1};
        double reversingFactor{1};
        double configuredDuration{};
        double duration{};
        double start{};
        double pendingEnd{1};
    };

    std::vector<Declaration> Parse(const std::string& css);
    std::vector<Declaration> Translate(const std::string& css);
    std::string ColorAndUnits(const std::string& value);
    double CssEase(double progress);
    bool SetOpacityTarget(OpacityTransition& transition, double alpha);
    bool AdvanceOpacity(OpacityTransition& transition, double seconds);
}
