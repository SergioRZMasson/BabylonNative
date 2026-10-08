#include "UiCss.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

// Host CSS frontend, adapted from the qualified native UiBridge/CssTransition.
// Layout, effects, resources and rendering remain exclusively in Core/RmlUI.
namespace
{
    std::string Trim(const std::string& value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return {};
        }
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }

    double Cubic(double parameter, double first, double second)
    {
        const double inverse = 1 - parameter;
        return 3 * inverse * inverse * parameter * first +
               3 * inverse * parameter * parameter * second + parameter * parameter * parameter;
    }

    double Progress(const LitePlayground::UiCss::OpacityTransition& transition, double seconds)
    {
        return transition.duration > 0
                   ? std::clamp((seconds - transition.start) / transition.duration, 0.0, 1.0)
                   : 1;
    }
}

namespace LitePlayground::UiCss
{
    std::string ColorAndUnits(const std::string& input)
    {
        static const std::regex rgba(
            R"(rgba\(\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*\))");
        std::string value;
        size_t consumed{};
        for (std::sregex_iterator match(input.begin(), input.end(), rgba), end; match != end;
             ++match)
        {
            value += input.substr(consumed, static_cast<size_t>(match->position()) - consumed);
            const auto authoredAlpha = (*match)[4].str();
            char* alphaEnd{};
            const double alpha = std::strtod(authoredAlpha.c_str(), &alphaEnd);
            if (!alphaEnd || alphaEnd == authoredAlpha.c_str() || *alphaEnd ||
                !std::isfinite(alpha) || alpha < 0 || alpha > 1)
            {
                throw std::runtime_error("Browser rgba alpha outside [0,1]: " + input);
            }
            auto decimal = authoredAlpha;
            auto point = decimal.find('.');
            if (point == std::string::npos)
            {
                point = decimal.size();
            }
            else
            {
                decimal.erase(point, 1);
            }
            point += 2;
            if (point >= decimal.size())
            {
                decimal.append(point - decimal.size(), '0');
            }
            else
            {
                decimal.insert(point, 1, '.');
            }
            const auto nonzero = decimal.find_first_not_of('0');
            decimal = nonzero == std::string::npos ? "0" : decimal.substr(nonzero);
            if (decimal.front() == '.')
            {
                decimal.insert(0, "0");
            }
            value += "rgba(" + (*match)[1].str() + "," + (*match)[2].str() + "," +
                     (*match)[3].str() + "," + decimal + "%)";
            consumed = static_cast<size_t>(match->position() + match->length());
        }
        value += input.substr(consumed);
        if (value.find("image(") == std::string::npos && value.find("url(") == std::string::npos)
        {
            static const std::regex pixels(R"(([0-9.])px\b)");
            value = std::regex_replace(value, pixels, "$1dp");
        }
        return value;
    }

    std::vector<Declaration> Parse(const std::string& css)
    {
        std::vector<Declaration> result;
        size_t begin{};
        int depth{};
        char quote{};
        for (size_t index = 0; index <= css.size(); ++index)
        {
            const char ch = index == css.size() ? ';' : css[index];
            if (quote)
            {
                if (ch == quote && (!index || css[index - 1] != '\\'))
                {
                    quote = 0;
                }
            }
            else if (ch == '\'' || ch == '"')
            {
                quote = ch;
            }
            else if (ch == '(')
            {
                ++depth;
            }
            else if (ch == ')')
            {
                --depth;
            }
            if (ch != ';' || quote || depth)
            {
                continue;
            }
            const auto declaration = Trim(css.substr(begin, index - begin));
            begin = index + 1;
            if (declaration.empty())
            {
                continue;
            }
            const auto colon = declaration.find(':');
            if (colon == std::string::npos)
            {
                throw std::runtime_error("Invalid CSS declaration: " + declaration);
            }
            auto name = Trim(declaration.substr(0, colon));
            auto value = Trim(declaration.substr(colon + 1));
            result.push_back({std::move(name), std::move(value)});
        }
        if (quote || depth)
        {
            throw std::runtime_error("Unbalanced CSS: " + css);
        }
        return result;
    }

    std::vector<Declaration> Translate(const std::string& css)
    {
        std::vector<Declaration> result;
        for (const auto& declaration : Parse(css))
        {
            auto name = declaration.name;
            auto value = declaration.value;
            if (name == "position" && value == "fixed")
            {
                value = "absolute";
            }
            else if (name == "inset")
            {
                for (const auto* side : {"left", "top", "right", "bottom"})
                {
                    result.push_back({side, ColorAndUnits(value)});
                }
                continue;
            }
            else if (name == "font-family")
            {
                if (value == "system-ui,Segoe UI,sans-serif" || value == "system-ui")
                {
                    value = "Segoe UI";
                }
                else if (value == "monospace")
                {
                    value = "Consolas";
                }
                else if (value.find(',') != std::string::npos)
                {
                    throw std::runtime_error("Unsupported font fallback stack: " + value);
                }
            }
            else if (name == "font")
            {
                static const std::regex font(
                    R"(^(?:([0-9]+)\s+)?([0-9.]+)px(?:/([0-9.]+))?\s+(.+)$)");
                std::smatch match;
                if (!std::regex_match(value, match, font))
                {
                    throw std::runtime_error("Unmapped browser font shorthand: " + value);
                }
                auto family = match[4].str();
                if (family == "monospace")
                {
                    family = "Consolas";
                }
                result.push_back({"font-family", family});
                result.push_back({"font-size", match[2].str() + "dp"});
                result.push_back({"font-weight", match[1].matched ? match[1].str() : "400"});
                if (match[3].matched)
                {
                    result.push_back({"line-height", match[3].str()});
                }
                continue;
            }
            else if (name == "text-shadow")
            {
                static const std::regex shadow(
                    R"(^(-?[0-9.]+(?:px|dp)?)\s+(-?[0-9.]+(?:px|dp)?)\s+([0-9.]+(?:px|dp)?)\s+(.+)$)");
                std::smatch match;
                if (!std::regex_match(value, match, shadow))
                {
                    throw std::runtime_error("Unmapped browser text shadow: " + value);
                }
                for (size_t component = 1; component <= 3; ++component)
                {
                    const auto length = match[component].str();
                    if (!length.ends_with("px") && !length.ends_with("dp") &&
                        std::stod(length) != 0)
                    {
                        throw std::runtime_error("Nonzero unitless text shadow length.");
                    }
                }
                name = "font-effect";
                value = "glow(0px " + match[3].str() + " " + match[1].str() + " " + match[2].str() +
                        " " + match[4].str() + ")";
            }
            else if (name == "border")
            {
                static const std::regex border(R"(^([0-9.]+(?:px|dp)?)\s+solid\s+(.+)$)");
                std::smatch match;
                if (!std::regex_match(value, match, border))
                {
                    throw std::runtime_error("Unmapped browser border: " + value);
                }
                result.push_back({"border-width", ColorAndUnits(match[1].str())});
                result.push_back({"border-color", ColorAndUnits(match[2].str())});
                continue;
            }
            else if (name == "background")
            {
                if (value.find("linear-gradient(#fff,#fff) center/") != std::string::npos)
                {
                    const std::string expected =
                        "linear-gradient(#fff,#fff) center/2px 22px no-repeat,"
                        "linear-gradient(#fff,#fff) center/22px 2px no-repeat";
                    if (value != expected)
                    {
                        throw std::runtime_error("Unsupported positioned browser gradient: " +
                                                 value);
                    }
                    result.push_back({"host-white-crosshair", "true"});
                    continue;
                }
                if (value.starts_with("radial-gradient(") || value.starts_with("linear-gradient("))
                {
                    name = "decorator";
                }
                else if (value.find("url(") != std::string::npos)
                {
                    static const std::regex image(
                        R"css(^(#[0-9a-fA-F]+)\s+url\("([^"]+)"\)\s+center/cover$)css");
                    std::smatch match;
                    if (!std::regex_match(value, match, image))
                    {
                        throw std::runtime_error("Unmapped browser background image: " + value);
                    }
                    result.push_back({"background-color", match[1].str()});
                    result.push_back({"host-image", match[2].str()});
                    continue;
                }
                else
                {
                    name = "background-color";
                }
            }
            else if (name == "mix-blend-mode")
            {
                if (value != "difference")
                {
                    throw std::runtime_error("Unsupported browser blend mode: " + value);
                }
                name = "host-white-difference";
            }
            else if (name == "image-rendering")
            {
                if (value != "pixelated")
                {
                    throw std::runtime_error("Unsupported browser image sampling: " + value);
                }
                name = "host-image-sampling";
            }
            else if (name == "transition")
            {
                static const std::regex transition(R"(^opacity ((?:[0-9]*\.)?[0-9]+)s ease$)");
                std::smatch match;
                if (!std::regex_match(value, match, transition))
                {
                    throw std::runtime_error("Unmapped browser transition: " + value);
                }
                name = "host-opacity-duration";
                value = match[1].str();
            }
            result.push_back({name, ColorAndUnits(value)});
        }
        return result;
    }

    double CssEase(double progress)
    {
        if (progress <= 0 || progress >= 1)
        {
            return std::clamp(progress, 0.0, 1.0);
        }
        double lower{};
        double upper{1};
        for (unsigned iteration = 0; iteration < 48; ++iteration)
        {
            const double parameter = (lower + upper) * 0.5;
            if (Cubic(parameter, 0.25, 0.25) < progress)
            {
                lower = parameter;
            }
            else
            {
                upper = parameter;
            }
        }
        return Cubic((lower + upper) * 0.5, 0.1, 1);
    }

    bool SetOpacityTarget(OpacityTransition& transition, double alpha)
    {
        alpha = std::clamp(alpha, 0.0, 1.0);
        if (!transition.initialized || transition.configuredDuration <= 0)
        {
            transition.value = alpha;
            transition.end = alpha;
            transition.reversingStart = alpha;
            transition.pending = false;
            transition.active = false;
            return true;
        }
        transition.pending = true;
        transition.pendingEnd = alpha;
        return false;
    }

    bool AdvanceOpacity(OpacityTransition& transition, double seconds)
    {
        const double previous = transition.value;
        if (!transition.initialized)
        {
            transition.initialized = true;
            return false;
        }
        const bool interrupted = transition.active;
        const double easedProgress = transition.active ? CssEase(Progress(transition, seconds)) : 1;
        if (transition.active)
        {
            transition.value = transition.from + (transition.end - transition.from) * easedProgress;
            if (Progress(transition, seconds) >= 1)
            {
                transition.active = false;
            }
        }
        if (transition.pending)
        {
            transition.pending = false;
            if (transition.pendingEnd != transition.end)
            {
                double factor{1};
                double reversingStart = transition.value;
                if (interrupted && transition.pendingEnd == transition.reversingStart)
                {
                    factor = std::clamp(std::abs(easedProgress * transition.reversingFactor +
                                                 (1 - transition.reversingFactor)),
                                        0.0, 1.0);
                    reversingStart = transition.end;
                }
                transition.from = transition.value;
                transition.end = transition.pendingEnd;
                transition.reversingStart = reversingStart;
                transition.reversingFactor = factor;
                transition.duration = transition.configuredDuration * factor;
                transition.start = seconds;
                transition.active = transition.from != transition.end && transition.duration > 0;
                if (!transition.active)
                {
                    transition.value = transition.end;
                }
            }
        }
        return transition.value != previous;
    }
}
