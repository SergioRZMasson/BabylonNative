"""Adapt only the existing OUTSIDE-Core native platform transport; never user C++."""
import hashlib
import json
import pathlib
import sys


def between(text, start, end, replacement):
    assert text.count(start) == 1, start
    assert end in text, end
    first = text.index(start)
    last = text.index(end, first)
    return text[:first] + replacement + text[last:]


def prepare(source, output):
    original = source.read_bytes()
    text = original.decode("utf-8").replace("\r\n", "\n")
    text = text.replace('#include "Platform.h"', '#include "Platform.h"\n#include "UiBridge.h"')
    text = text.replace("#include <regex>\n", "")
    text = text.replace("        std::string tag;", "        bl_UiElement native{};\n        std::string tag;")
    for line in [
        "    HWND s_overlay{};\n",
        "    std::vector<uint8_t> s_hud;\n",
        "    std::map<std::string, std::vector<uint8_t>> s_icons;\n",
        "        DestroyWindow(s_overlay);\n",
    ]:
        assert line in text, line
        text = text.replace(line, "")
    text = between(text, "#if defined(LITE_MINECRAFT_NO_UI)\n    uint64_t",
                   "\n    double Clock()", "\n    bool s_fixedClock{};\n")
    text = text.replace("        return std::chrono::duration<double, std::milli>",
                        "        if (s_fixedClock)\n        {\n            return s_time;\n        }\n"
                        "        return std::chrono::duration<double, std::milli>")
    text = between(text, "    std::vector<uint8_t> DecodeIcon(", "\n}\n\nnamespace bbl", "")
    text = between(text, "#if defined(LITE_MINECRAFT_NO_UI)\n        if (tag",
                   "        Element element{};", "")
    text = text.replace("    UiElementHandle ui_create_element(Engine&,",
                        "    UiElementHandle ui_create_element(Engine& engine,")
    text = text.replace("        element.tag = tag;",
                        "        element.tag = tag;\n"
                        "        element.native = MinecraftUi::Create(engine, tag);")
    text = text.replace("    UiElementHandle ui_document_root(Engine&, UiDocumentPart)",
                        "    UiElementHandle ui_document_root(Engine& engine, UiDocumentPart)")
    text = text.replace("        return 0;\n    }\n\n    void ui_append_child",
                        "        s_elements.at(0).native = MinecraftUi::Root(engine);\n"
                        "        return 0;\n    }\n\n    void ui_append_child")
    text = text.replace("        s_elements.at(parent).children.push_back(child);",
                        "        MinecraftUi::Append(s_elements.at(parent).native,\n"
                        "                            s_elements.at(child).native);\n"
                        "        s_elements.at(parent).children.push_back(child);")
    text = text.replace("    void ui_append_to_root(Engine& engine, UiElementHandle child)\n    {",
                        "    void ui_append_to_root(Engine& engine, UiElementHandle child)\n    {\n"
                        "        ui_document_root(engine, UiDocumentPart::Body);")
    text = between(text, '        if (name == "style")', "\n    void ui_set_style_property",
                   "        MinecraftUi::Attribute(item.native, name, value);\n    }\n")
    text = text.replace("        s_elements.at(element).styles[name] = value;",
                        "        MinecraftUi::Property(s_elements.at(element).native, name, value);\n"
                        "        s_elements.at(element).styles[name] = value;")
    text = text.replace("        s_elements.at(element).text = value;",
                        "        MinecraftUi::Text(s_elements.at(element).native, value);\n"
                        "        s_elements.at(element).text = value;")
    text = between(text, "#if !defined(LITE_MINECRAFT_NO_UI)",
                   "        if (interactive)", "")
    text = text.replace("    void TickPlatform(double)\n    {\n        s_time = Clock();",
                        "    void TickPlatform(double deltaMs)\n    {\n"
                        "        s_time = s_fixedClock ? s_time + deltaMs : Clock();")
    text = between(text, "    void PresentHUD()", "    bool WritePNG(",
                   "    void PresentHUD()\n    {\n"
                   '        throw std::runtime_error("GDI HUD forbidden in RmlUI host.");\n'
                   "    }\n\n    uint64_t HudBackingRenders()\n    {\n"
                   "        return 0;\n    }\n\n")
    text = between(text, "    void CaptureHUD(", "\n}\n",
                   "    void SetFixedPlatformClock(bool enabled)\n    {\n"
                   "        s_fixedClock = enabled;\n        s_time = 0;\n    }\n")
    for forbidden in ["CreateDIBSection", "CreateCompatibleDC", "UpdateLayeredWindow",
                      "StretchDIBits", "DrawTextW", "LITE_MINECRAFT_NO_UI", "s_overlay"]:
        assert forbidden not in text, forbidden
    output.mkdir(parents=True, exist_ok=True)
    generated = output / "PlatformTransport.cpp"
    if not generated.exists() or generated.read_text(encoding="utf-8") != text:
        generated.write_text(text, encoding="utf-8")
    (output / "platform-provenance.json").write_text(json.dumps({
        "source": str(source), "sourceSha256": hashlib.sha256(original).hexdigest(),
        "transportSha256": hashlib.sha256(text.encode()).hexdigest(),
        "engineAlgorithmsCopied": False, "gdiBacking": False,
        "adaptation": "input/file/WIC outside-Core reuse; retained C99 UI forwarding; fixed clock",
    }, indent=2), encoding="utf-8")


if __name__ == "__main__":
    prepare(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
