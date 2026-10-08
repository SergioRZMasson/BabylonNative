#include "UiBridge.h"
#include "CssTransition.h"
#include <iostream>
#include <cmath>
#include <source_location>
#include <stdexcept>

namespace bbl
{
    bl_String String(const char* value)
    {
        return {value, std::char_traits<char>::length(value)};
    }

    bl_String String(const std::string& value)
    {
        return {value.data(), value.size()};
    }
}

namespace LiteMinecraft
{
    bl_NativeEngineOptions NativeOptions()
    {
        throw std::runtime_error("Translation tests must not initialize UI/graphics.");
    }
}

namespace
{
    void Require(bool condition,
                 const std::source_location location = std::source_location::current())
    {
        if (!condition)
        {
            throw std::runtime_error("CSS translation assertion failed at line " +
                                     std::to_string(location.line()));
        }
    }

    void Reject(const std::string& css)
    {
        bool rejected{};
        try
        {
            MinecraftUi::Translate(css);
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
    using namespace MinecraftUi;
    try
    {
        Require(ColorAndUnits("rgba(30,90,150,0.35)") == "rgba(30,90,150,35%)");
        Require(ColorAndUnits("0 1px 2px #000") == "0 1dp 2dp #000");
        Require(ColorAndUnits("rgba(0,0,0,.055)") == "rgba(0,0,0,5.5%)");
        Require(ColorAndUnits("rgba(0,0,0,0.123456789)") == "rgba(0,0,0,12.3456789%)");
        const auto declaration =
            Translate("bbl-transform:translate(-50%,-50%);font-family:monospace;"
                      "--bbl-background-color:rgba(0,0,0,0.55);border-image:none;");
        Require(declaration.size() == 3);
        Require(declaration[0].name == "transform");
        Require(declaration[0].value == "translate(-50%,-50%)");
        Require(declaration[1].value == "Consolas");
        Require(declaration[2].name == "background-color");
        Require(declaration[2].value == "rgba(0,0,0,55%)");
        Require(Translate("decorator:image(\"asset;name.png\" cover);").size() == 1);
        Require(Translate("background-clip:border-box;background-color:#fff;").size() == 1);
        const auto shadow = Translate("text-shadow:0 1px 3px #000;");
        Require(shadow.size() == 1 && shadow[0].name == "font-effect" &&
                shadow[0].value == "glow(0dp 3dp 0 1dp #000)");
        Reject("text-shadow:1 1px 2px #000;");
        Reject("background-clip:border-box;border:2px #fff;");
        Reject("font-family:unknown,other;");
        Reject("--bbl-background-image:bad;");
        Reject("width");
        Reject("transform:translate(2px;");
        Reject("color:rgba(0,0,0,2);");
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
        Require(!SetOpacityTarget(coalesced, 1));
        Require(!AdvanceOpacity(coalesced, 2));
        Require(AdvanceOpacity(coalesced, 2.1));
        const auto beginning = coalesced.start;
        Require(!SetOpacityTarget(coalesced, 1));
        Require(AdvanceOpacity(coalesced, 2.15));
        Require(coalesced.start == beginning);
        std::cout << "CSS colors/density/font/metadata/strict refusals passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
