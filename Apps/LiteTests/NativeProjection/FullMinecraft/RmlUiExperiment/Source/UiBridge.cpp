#include "UiBridge.h"
#include "CssTransition.h"
#include "Platform.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace
{
    struct Element
    {
        bl_UiElement native{};
        std::string tag;
        std::string text;
        std::string originalCss;
        std::map<std::string, std::string> properties;
        std::vector<bl_UiElement> decorations;
        MinecraftUi::OpacityTransition opacity;
        bool browserShrinkToFit{};
    };

    bl_UiContext s_context{};
    bool s_contextCreated{};
    bl_UiElement s_root{};
    std::shared_ptr<bbl::EngineState> s_engineState;
    bl_UiContext s_crossContext{};
    bool s_crossCreated{};
    bl_UiElement s_crossElement{};
    uint64_t s_crossSourceId{};
    double s_density{1};
    uint32_t s_width{1280};
    bl_UiElement s_underwater{};
    bool s_underwaterKnown{};
    std::map<uint64_t, Element> s_elements;
    uint64_t s_nextElement{};
    std::map<std::string, std::string> s_images;
    uint64_t s_mutations{};

    uint64_t Key(bl_UiElement element)
    {
        for (const auto& [key, record] : s_elements)
        {
            if (std::memcmp(&record.native, &element, sizeof(element)) == 0)
            {
                return key;
            }
        }
        throw std::runtime_error("Unknown host DOM identity.");
    }

    void Record(bl_UiElement element, const std::string& tag)
    {
        Element record{};
        record.native = element;
        record.tag = tag;
        s_elements.emplace(++s_nextElement, std::move(record));
    }

    std::string Trim(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return {};
        }
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }

    std::vector<uint8_t> Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Missing host resource: " + path.string());
        }
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    void Require(bl_Status status, const std::string& operation)
    {
        if (status != BL_OK)
        {
            throw std::runtime_error(operation + " failed, C99 status " +
                                     std::to_string(static_cast<int>(status)));
        }
    }

    void Font(const std::filesystem::path& path, const char* family, uint32_t weight,
              bool fallback = false)
    {
        const auto bytes = Read(path);
        Require(bl_loadUiFont(s_context, {bytes.data(), bytes.size()}, bbl::String(family), weight,
                              false, fallback),
                "Load host system font " + path.string());
    }

    void Ensure(bbl::Engine& engine)
    {
        if (s_contextCreated)
        {
            return;
        }
        const auto native = LiteMinecraft::NativeOptions();
        s_width = native.target.width;
        bl_UiContextOptions options{};
        options.target = native.target;
        options.target.firstViewId = 64;
        options.target.viewCount = 192;
        options.densityRatio = 1;
        Require(bl_createUiContext(engine.state->engine, &options, &s_context),
                "Create optional UI-ON C99 context");
        s_contextCreated = true;
        s_engineState = engine.state;
        Require(bl_getUiRoot(s_context, &s_root), "Get retained UI root");
        Record(s_root, "body");
        wchar_t windows[MAX_PATH]{};
        if (!GetWindowsDirectoryW(windows, MAX_PATH))
        {
            throw std::runtime_error("Cannot locate licensed installed system fonts.");
        }
        const auto fonts = std::filesystem::path(windows) / L"Fonts";
        Font(fonts / L"segoeui.ttf", "Segoe UI", 400);
        Font(fonts / L"seguisb.ttf", "Segoe UI", 600);
        Font(fonts / L"consola.ttf", "Consolas", 400);
        Font(fonts / L"consolab.ttf", "Consolas", 700);
        Font(fonts / L"seguisym.ttf", "Segoe UI Symbol", 400, true);
        MinecraftUi::Property(s_root, "font-family", "Segoe UI");
        MinecraftUi::Property(s_root, "font-size", "16px");
    }

    std::string RegisterImage(const std::string& source)
    {
        if (s_images.contains(source))
        {
            return s_images.at(source);
        }
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        const auto file = std::filesystem::path(LITE_MINECRAFT_ORIGINAL_ASSETS) /
                          std::filesystem::path(source).filename();
        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&factory));
        if (SUCCEEDED(result))
        {
            result = factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnLoad, &decoder);
        }
        if (SUCCEEDED(result))
        {
            result = decoder->GetFrame(0, &frame);
        }
        if (SUCCEEDED(result))
        {
            result = factory->CreateFormatConverter(&converter);
        }
        if (SUCCEEDED(result))
        {
            result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                           WICBitmapDitherTypeNone, nullptr, 0,
                                           WICBitmapPaletteTypeCustom);
        }
        UINT width{};
        UINT height{};
        if (SUCCEEDED(result))
        {
            result = converter->GetSize(&width, &height);
        }
        if (FAILED(result) || !width || !height || width > 16384 || height > 16384)
        {
            throw std::runtime_error("Original hotbar image decode failed: " + file.string());
        }
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        result = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()),
                                       pixels.data());
        if (FAILED(result))
        {
            throw std::runtime_error("Original hotbar pixel decode failed: " + file.string());
        }
        const auto nativeSource = "minecraft-image-" + std::to_string(s_images.size());
        Require(bl_registerUiImage(s_context, bbl::String(nativeSource),
                                   {pixels.data(), pixels.size()}, width, height),
                "Register original hotbar " + source);
        Require(bl_setUiImageSampling(s_context, bbl::String(nativeSource), true),
                "Nearest original hotbar sampling");
        s_images.emplace(source, nativeSource);
        return nativeSource;
    }

    void Set(bl_UiElement element, const std::string& name, const std::string& value)
    {
        const auto key = Key(element);
        auto& record = s_elements.at(key);
        const auto current = record.properties.find(name);
        if (current != record.properties.end() && current->second == value)
        {
            return;
        }
        if (name == "transition")
        {
            static const std::regex opacityTransition(R"(^opacity ((?:[0-9]*\.)?[0-9]+)s ease$)");
            std::smatch match;
            if (!std::regex_match(value, match, opacityTransition))
            {
                throw std::runtime_error("Unmapped browser CSS transition: " + value);
            }
            record.opacity.configuredDuration = std::stod(match[1].str());
            record.properties[name] = value;
            ++s_mutations;
            return;
        }
        if (name == "opacity")
        {
            char* end{};
            const double alpha = std::strtod(value.c_str(), &end);
            if (!end || end == value.c_str() || *end || !std::isfinite(alpha))
            {
                throw std::runtime_error("Invalid browser opacity: " + value);
            }
            if (!MinecraftUi::SetOpacityTarget(record.opacity, alpha))
            {
                record.properties[name] = value;
                ++s_mutations;
                return;
            }
        }
        Require(bl_setUiProperty(element, bbl::String(name), bbl::String(value)),
                "UI property " + name + ":" + value + " on " + record.tag);
        record.properties[name] = value;
        if (name == "box-shadow" && value == "inset 0 0 220dp 60dp rgba(4,26,60,85%)")
        {
            s_underwater = element;
            s_underwaterKnown = true;
        }
        ++s_mutations;
        if (key == s_crossSourceId && s_crossCreated)
        {
            Set(s_crossElement, name, value);
        }
    }

    void ShrinkToFit(Element& record)
    {
        if (!record.browserShrinkToFit || !record.properties.contains("left") ||
            record.properties.at("left") != "50%" || record.properties.contains("width"))
        {
            return;
        }
        double horizontalPadding{};
        if (record.properties.contains("padding"))
        {
            std::istringstream values(record.properties.at("padding"));
            std::vector<double> sizes;
            std::string value;
            while (values >> value)
            {
                char* end{};
                double size = std::strtod(value.c_str(), &end);
                if (end && std::string(end) == "dp")
                {
                    size *= s_density;
                }
                else if (!end || (*end && std::string(end) != "px"))
                {
                    throw std::runtime_error("Unmapped shrink-to-fit padding unit.");
                }
                sizes.push_back(size);
            }
            if (sizes.size() == 1)
            {
                horizontalPadding = 2 * sizes[0];
            }
            else if (sizes.size() == 2 || sizes.size() == 3)
            {
                horizontalPadding = 2 * sizes[1];
            }
            else if (sizes.size() == 4)
            {
                horizontalPadding = sizes[1] + sizes[3];
            }
            else
            {
                throw std::runtime_error("Unmapped shrink-to-fit padding.");
            }
        }
        const double available =
            std::max(0.0, static_cast<double>(s_width) * 0.5 - horizontalPadding);
        std::ostringstream width;
        width << std::setprecision(17) << available << "px";
        const auto current = record.properties.find("max-width");
        if (current == record.properties.end() || current->second != width.str())
        {
            // Browser absolute left:50%/auto width shrinks against its remaining containing block.
            Require(
                bl_setUiProperty(record.native, bbl::String("max-width"), bbl::String(width.str())),
                "Apply browser absolute shrink-to-fit width");
            record.properties["max-width"] = width.str();
            ++s_mutations;
        }
    }

    void Crosshair(bl_UiElement element)
    {
        auto& record = s_elements.at(Key(element));
        if (!record.decorations.empty())
        {
            return;
        }
#if defined(LITE_UI_HAS_WHITE_DIFFERENCE)
        bl_UiContextOptions options{};
        options.target = LiteMinecraft::NativeOptions().target;
        options.target.firstViewId = 32;
        options.target.viewCount = 32;
        options.densityRatio = s_density;
        Require(bl_createUiContext(s_engineState->engine, &options, &s_crossContext),
                "Create exclusive white-difference crosshair context");
        s_crossCreated = true;
        Require(bl_setUiWhiteDifference(s_crossContext, true),
                "Enable exact white-only destination-dependent difference");
        bl_UiElement root{};
        Require(bl_getUiRoot(s_crossContext, &root), "Get white-difference root");
        Require(bl_setUiProperty(root, bbl::String("pointer-events"), bbl::String("none")),
                "Disable crosshair input");
        Require(bl_createUiElement(s_crossContext, bbl::String("div"), &s_crossElement),
                "Create authored crosshair container");
        Record(s_crossElement, "div");
        for (const auto& [name, value] : record.properties)
        {
            Set(s_crossElement, name, value);
        }
        Require(bl_appendUiChild(root, s_crossElement), "Append white-difference crosshair");
        s_crossSourceId = Key(element);
#else
        throw std::runtime_error("Original crosshair requires approved "
                                 "bl_setUiWhiteDifference Core capability; no white fallback.");
#endif
        // Three disjoint rectangles preserve the original union and group opacity.
        const char* rectangles[] = {
            "position:absolute;left:10dp;top:0dp;width:2dp;height:22dp;background-color:#fff;",
            "position:absolute;left:0dp;top:10dp;width:10dp;height:2dp;background-color:#fff;",
            "position:absolute;left:12dp;top:10dp;width:10dp;height:2dp;background-color:#fff;"};
        for (const auto* css : rectangles)
        {
            bl_UiElement child{};
            Require(bl_createUiElement(s_crossContext, bbl::String("div"), &child),
                    "Create retained crosshair decoration");
            Record(child, "div");
            MinecraftUi::Attribute(child, "style", css);
            Require(bl_appendUiChild(s_crossElement, child), "Append crosshair decoration");
            record.decorations.push_back(child);
        }
    }
}

namespace MinecraftUi
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
            auto decimal = (*match)[4].str();
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
            std::ostringstream color;
            color << "rgba(" << (*match)[1].str() << ',' << (*match)[2].str() << ','
                  << (*match)[3].str() << ',' << decimal << "%)";
            value += color.str();
            consumed = static_cast<size_t>(match->position() + match->length());
        }
        value += input.substr(consumed);
        // CSS logical pixels scale at DPI; URLs are copied, not unit-translated.
        if (value.find("image(") == std::string::npos && value.find("url(") == std::string::npos)
        {
            static const std::regex pixels(R"(([0-9.])px\b)");
            value = std::regex_replace(value, pixels, "$1dp");
        }
        return value;
    }

    std::vector<Declaration> Translate(const std::string& css)
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
            if (name == "bbl-transform")
            {
                name = "transform";
            }
            else if (name == "--bbl-background-color")
            {
                name = "background-color";
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
            else if (name == "background" && (value.starts_with("#") || value.starts_with("rgba(")))
            {
                if (value.find("url(") != std::string::npos ||
                    value.find("gradient(") != std::string::npos)
                {
                    throw std::runtime_error("Unlowered browser background image/gradient.");
                }
                name = "background-color";
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
            else if (name == "background-clip")
            {
                if (value != "border-box" || css.find("border:") != std::string::npos ||
                    css.find("border-width:") != std::string::npos)
                {
                    throw std::runtime_error("Background clip requires nonzero-border support.");
                }
                // Emitted borderless elements have equal padding and border bounds.
                continue;
            }
            else if (name == "border-image" && value == "none")
            {
                continue;
            }
            else if (name.starts_with("--"))
            {
                const std::map<std::string, std::string> metadata = {
                    {"--bbl-background-image", "none"},
                    {"--bbl-background-size", "auto"},
                    {"--bbl-background-position", "0% 0%"},
                    {"--bbl-background-repeat", "repeat"},
                    {"--bbl-background-origin", "padding-box"},
                    {"--bbl-background-attachment", "scroll"},
                    {"--bbl-authored-display", "1"},
                    {"--bbl-absolute-inline", "1"}};
                const auto expected = metadata.find(name);
                if (expected == metadata.end() || expected->second != value)
                {
                    throw std::runtime_error("Unmapped compiler CSS metadata: " + declaration);
                }
                if (name == "--bbl-absolute-inline")
                {
                    result.push_back({"display", "inline-block"});
                }
                continue;
            }
            result.push_back({name, ColorAndUnits(value)});
        }
        if (quote || depth)
        {
            throw std::runtime_error("Unbalanced CSS: " + css);
        }
        return result;
    }

    bl_UiElement Create(bbl::Engine& engine, const std::string& tag)
    {
        Ensure(engine);
        bl_UiElement element{};
        Require(bl_createUiElement(s_context, bbl::String(tag), &element), "Create UI " + tag);
        Record(element, tag);
        if (tag == "a" || tag == "input" || tag == "canvas")
        {
            Property(element, "display", "none");
        }
        return element;
    }

    bl_UiElement Root(bbl::Engine& engine)
    {
        Ensure(engine);
        return s_root;
    }

    void Append(bl_UiElement parent, bl_UiElement child)
    {
        Require(bl_appendUiChild(parent, child), "Append retained UI child");
    }

    void Attribute(bl_UiElement element, const std::string& name, const std::string& value)
    {
        if (name != "style")
        {
            Require(bl_setUiAttribute(element, bbl::String(name), bbl::String(value)),
                    "Set UI attribute " + name);
            return;
        }
        auto& record = s_elements.at(Key(element));
        if (record.originalCss == value)
        {
            return;
        }
        const auto declarations = Translate(value);
        record.browserShrinkToFit = value.find("--bbl-absolute-inline:1") != std::string::npos &&
                                    value.find("--bbl-authored-display:1") == std::string::npos;
        for (const auto& declaration : declarations)
        {
            auto nativeValue = declaration.value;
            if (declaration.name == "decorator" &&
                declaration.value.starts_with("linear-gradient(#fff,#fff) padding-box /"))
            {
                const std::string expected =
                    "linear-gradient(#fff,#fff) padding-box / 2dp 22dp / 50% 50%,"
                    "linear-gradient(#fff,#fff) padding-box / 22dp 2dp / 50% 50%";
                if (declaration.value != expected)
                {
                    throw std::runtime_error("Unmapped positioned crosshair gradient.");
                }
                Crosshair(element);
                continue;
            }
            if (declaration.name == "decorator" && declaration.value.starts_with("image(\""))
            {
                const auto end = declaration.value.find('"', 7);
                if (end == std::string::npos)
                {
                    throw std::runtime_error("Invalid original hotbar decorator.");
                }
                const auto source = RegisterImage(declaration.value.substr(7, end - 7));
                nativeValue.replace(7, end - 7, source);
            }
            Property(element, declaration.name, nativeValue);
        }
        record.originalCss = value;
        ShrinkToFit(record);
    }

    void Property(bl_UiElement element, const std::string& name, const std::string& value)
    {
        for (const auto& declaration : Translate(name + ":" + value + ";"))
        {
            Set(element, declaration.name, declaration.value);
        }
    }

    void Text(bl_UiElement element, const std::string& value)
    {
        auto& record = s_elements.at(Key(element));
        if (record.text != value)
        {
            Require(bl_setUiText(element, bbl::String(value)), "Update plain UI text");
            record.text = value;
            ++s_mutations;
        }
    }

    void Update(double seconds)
    {
        for (auto& [key, record] : s_elements)
        {
            static_cast<void>(key);
            if (AdvanceOpacity(record.opacity, seconds))
            {
                std::ostringstream alpha;
                alpha << std::setprecision(17) << record.opacity.value;
                Require(bl_setUiProperty(record.native, bbl::String("opacity"),
                                         bbl::String(alpha.str())),
                        "Advance exact browser opacity ease transition");
            }
        }
        if (s_crossCreated)
        {
            Require(bl_updateUi(s_crossContext, seconds), "Update white-difference crosshair");
        }
        Require(bl_updateUi(s_context, seconds), "Update retained RmlUI");
    }

    void Render()
    {
        if (s_crossCreated)
        {
            Require(bl_renderUi(s_crossContext), "Render exact white-difference crosshair");
        }
        Require(bl_renderUi(s_context), "Submit retained bgfx UI after 3D");
    }

    void Dispose()
    {
        if (s_crossCreated)
        {
            Require(bl_disposeUiContext(s_crossContext), "Dispose white-difference context");
            s_crossContext = {};
            s_crossCreated = false;
            s_crossElement = {};
            s_crossSourceId = 0;
        }
        if (s_contextCreated)
        {
            Require(bl_disposeUiContext(s_context), "Dispose retained UI before borrowed engine");
            s_context = {};
            s_contextCreated = false;
            s_root = {};
            s_elements.clear();
            s_images.clear();
            s_underwater = {};
            s_underwaterKnown = false;
            s_engineState.reset();
        }
    }

    void Viewport(uint32_t width, uint32_t height, double density)
    {
        s_density = density;
        s_width = width;
        if (s_crossCreated)
        {
            Require(bl_setUiViewport(s_crossContext, width, height, density),
                    "Resize/DPI white-difference crosshair");
        }
        if (s_contextCreated)
        {
            Require(bl_setUiViewport(s_context, width, height, density), "Resize/DPI UI");
        }
        for (auto& [key, record] : s_elements)
        {
            static_cast<void>(key);
            ShrinkToFit(record);
        }
    }

    void Input(const bl_UiInput& input)
    {
        if (s_contextCreated)
        {
            bool consumed{};
            Require(bl_processUiInput(s_context, &input, &consumed), "Dispatch native UI input");
        }
    }

    bl_UiStats Stats()
    {
        bl_UiStats stats{};
        Require(bl_getUiStats(s_context, &stats), "Observe C99 UI statistics");
        if (s_crossCreated)
        {
            bl_UiStats cross{};
            Require(bl_getUiStats(s_crossContext, &cross), "Observe exact crosshair statistics");
            stats.geometryCompileCount += cross.geometryCompileCount;
            stats.geometryReleaseCount += cross.geometryReleaseCount;
            stats.textureCreateCount += cross.textureCreateCount;
            stats.textureReleaseCount += cross.textureReleaseCount;
            stats.drawCount += cross.drawCount;
            stats.uploadedBytes += cross.uploadedBytes;
            stats.liveGeometryCount += cross.liveGeometryCount;
            stats.liveTextureCount += cross.liveTextureCount;
            stats.liveElementCount += cross.liveElementCount;
        }
        return stats;
    }

    uint64_t Mutations()
    {
        return s_mutations;
    }

    void ForceUnderwaterForUiTest()
    {
        if (!s_underwaterKnown)
        {
            throw std::runtime_error("Original underwater UI element was not bound.");
        }
        Property(s_underwater, "opacity", "1");
    }

    void WriteDom(const std::filesystem::path& file)
    {
        std::ofstream stream(file);
        for (const auto& [id, element] : s_elements)
        {
            stream << id << ' ' << element.tag << " text=" << std::quoted(element.text) << '\n';
            for (const auto& [name, value] : element.properties)
            {
                stream << "  " << name << ':' << value << '\n';
            }
        }
        for (const auto& [source, alias] : s_images)
        {
            stream << "image " << std::quoted(source) << " => " << std::quoted(alias)
                   << " sampling=point\n";
        }
    }
}
