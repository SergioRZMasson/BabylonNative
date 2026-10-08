#pragma once

#include "../../Source/C99Client.h"

namespace MinecraftUi
{
    struct Declaration
    {
        std::string name;
        std::string value;
    };

    std::vector<Declaration> Translate(const std::string& css);
    std::string ColorAndUnits(const std::string& value);
    bl_UiElement Create(bbl::Engine& engine, const std::string& tag);
    bl_UiElement Root(bbl::Engine& engine);
    void Append(bl_UiElement parent, bl_UiElement child);
    void Attribute(bl_UiElement element, const std::string& name, const std::string& value);
    void Property(bl_UiElement element, const std::string& name, const std::string& value);
    void Text(bl_UiElement element, const std::string& value);
    void Update(double seconds);
    void Render();
    void Dispose();
    void Viewport(uint32_t width, uint32_t height, double density);
    void Input(const bl_UiInput& input);
    bl_UiStats Stats();
    uint64_t Mutations();
    void ForceUnderwaterForUiTest();
    void WriteDom(const std::filesystem::path& file);
    int RunBoundaryTest(const std::string& requiredEffect, const std::filesystem::path& capture);
}

namespace LiteMinecraft
{
    void SetFixedPlatformClock(bool enabled);
}
