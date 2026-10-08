#include "UiCss.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace
{
    void Require(bool condition)
    {
        if (!condition)
        {
            throw std::runtime_error("Native browser CSS frontend assertion failed.");
        }
    }

    void Reject(const std::string& value)
    {
        bool rejected{};
        try
        {
            LitePlayground::UiCss::Translate(value);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        Require(rejected);
    }
}

int main()
{
    using namespace LitePlayground::UiCss;
    try
    {
        Require(ColorAndUnits("rgba(0,0,0,.055)") == "rgba(0,0,0,5.5%)");
        Require(ColorAndUnits("rgba(0,0,0,0.123456789)") == "rgba(0,0,0,12.3456789%)");
        Require(ColorAndUnits("0 1px 2px #000") == "0 1dp 2dp #000");
        const auto font = Translate("font:600 13px/1 monospace;");
        Require(font.size() == 4 && font[0].value == "Consolas" && font[1].value == "13dp" &&
                font[2].value == "600" && font[3].value == "1");
        const auto shadow = Translate("text-shadow:0 1px 3px #000;");
        Require(shadow.size() == 1 && shadow[0].name == "font-effect" &&
                shadow[0].value == "glow(0dp 3dp 0 1dp #000)");
        Require(Translate("position:fixed;inset:0;").size() == 5);
        Require(Translate("background:#222 url(\"asset;name.png\") center/cover;")[1].value ==
                "asset;name.png");
        Reject("font-family:unknown,other;");
        Reject("transition:transform 1s linear;");
        Reject("text-shadow:1 1px 2px #000;");
        Reject("color:rgba(0,0,0,2);");
        Reject("width");
        Reject("transform:translate(2px;");
        Require(std::abs(CssEase(0.5) - 0.802403387584857) < 1e-12);
        OpacityTransition transition{};
        transition.configuredDuration = 0.25;
        Require(SetOpacityTarget(transition, 0));
        Require(!AdvanceOpacity(transition, 0));
        Require(!SetOpacityTarget(transition, 1));
        Require(!AdvanceOpacity(transition, 1));
        Require(AdvanceOpacity(transition, 1.125));
        Require(std::abs(transition.value - CssEase(0.5)) < 1e-12);
        Require(!SetOpacityTarget(transition, 1));
        Require(AdvanceOpacity(transition, 1.25));
        Require(transition.value == 1 && !transition.active);
        Require(!SetOpacityTarget(transition, 0));
        Require(!AdvanceOpacity(transition, 2));
        Require(AdvanceOpacity(transition, 2.125));
        const double current = transition.value;
        Require(!SetOpacityTarget(transition, 1));
        Require(!AdvanceOpacity(transition, 2.125));
        Require(transition.from == current);
        Require(std::abs(transition.duration - 0.25 * CssEase(0.5)) < 1e-12);
        OpacityTransition coalesced{};
        coalesced.configuredDuration = 0.2;
        Require(SetOpacityTarget(coalesced, 0));
        Require(!AdvanceOpacity(coalesced, 0));
        Require(!SetOpacityTarget(coalesced, 1));
        Require(!SetOpacityTarget(coalesced, 0));
        Require(!AdvanceOpacity(coalesced, 1));
        Require(coalesced.value == 0 && !coalesced.active);
        std::puts("Browser CSS/Rml lowering and exact easing/coalescing/reversal passed.");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
