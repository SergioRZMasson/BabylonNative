#include "StandardBinding.h"
#include "UiBridge.h"
#include "Platform.h"
#include <bgfx/bgfx.h>
#include <cstdio>

namespace MinecraftUi
{
    int RunBoundaryTest(const std::string& requiredEffect, const std::filesystem::path& capture)
    {
        auto engine = bbl::babylon::createEngine({"UI binding qualification", 1280, 720});
        const auto root = Root(engine);
        const auto button = Create(engine, "button");
        Attribute(button, "style",
                  "position:absolute;left:20px;top:20px;width:180px;height:40px;"
                  "background-color:rgba(0,0,0,0.35);color:#fff;font-family:system-ui;"
                  "font-size:13px;border-radius:4px;font-effect:glow(0px 2px 0 1px #000);");
        Text(button, "Retained C99 UI button");
        Append(root, button);
        if (!requiredEffect.empty())
        {
            if (requiredEffect == "box-shadow")
            {
                Property(button, "box-shadow", "inset 0 0 220px 60px rgba(4,26,60,0.85)");
            }
            else if (requiredEffect == "difference")
            {
                Property(button, "mix-blend-mode", "difference");
            }
            else
            {
                throw std::runtime_error("Unknown required effect: " + requiredEffect);
            }
        }
        const auto image = Create(engine, "div");
        Attribute(image, "style",
                  "position:absolute;left:20px;top:100px;width:50px;height:50px;"
                  "border:2px rgba(255,255,255,0.25);border-radius:4px;"
                  "--bbl-background-color:#222;"
                  "decorator:image(\"/minecraft/voxelpack/dirt_grass.png\" cover);");
        Append(root, image);
        const auto submit = [&](double seconds)
        {
            bbl::Check(bl_renderFrame(engine.state->engine, 0));
            bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                               UINT32_C(0x1c2c40ff), 0.0f, 0);
            bgfx::touch(0);
            Update(seconds);
            Render();
            bgfx::frame();
        };
        submit(0);
        const auto initial = Stats();
        if (!initial.drawCount || !initial.liveGeometryCount || !initial.liveTextureCount)
        {
            throw std::runtime_error("UI boundary did not render geometry/font/image.");
        }
        for (uint32_t frame = 1; frame <= 1000; ++frame)
        {
            Text(button, "Retained C99 UI button");
            Property(image, "bbl-transform", "scale(1)");
            submit(static_cast<double>(frame) / 60.0);
        }
        const auto stable = Stats();
        if (stable.geometryCompileCount != initial.geometryCompileCount ||
            stable.textureCreateCount != initial.textureCreateCount ||
            stable.uploadedBytes != initial.uploadedBytes)
        {
            throw std::runtime_error("Unchanged retained UI recreated GPU resources.");
        }

        struct EventState
        {
            bool clicked{};
            bl_Status mutation{BL_NOT_READY};
            std::exception_ptr error;
        };

        EventState state{};
        bl_UiListenerToken listener{};
        bbl::Check(bl_addUiEventListener(
            button, BL_UI_EVENT_CLICK,
            [](void* user, const bl_UiEvent* event)
            {
                auto& value = *static_cast<EventState*>(user);
                value.clicked = true;
                try
                {
                    Text(event->currentTarget, "Clicked");
                    value.mutation = BL_OK;
                }
                catch (...)
                {
                    value.error = std::current_exception();
                    value.mutation = BL_HOST_ERROR;
                }
            },
            &state, &listener));
        bl_UiInput input{};
        input.kind = BL_UI_POINTER_MOVE;
        input.x = 40;
        input.y = 40;
        Input(input);
        input.kind = BL_UI_POINTER_DOWN;
        Input(input);
        input.kind = BL_UI_POINTER_UP;
        Input(input);
        if (state.error)
        {
            std::rethrow_exception(state.error);
        }
        if (!state.clicked || state.mutation != BL_OK)
        {
            throw std::runtime_error("Native retained UI event/dispatch mutation failed.");
        }
        bbl::Check(bl_removeUiEventListener(button, listener));
        Property(image, "bbl-transform", "scale(1.12)");
        Property(image, "border-color", "#fff");
        Property(button, "font-family", "monospace");
        Property(button, "font-size", "15px");
        submit(1001.0 / 60.0);
        const auto mutated = Stats();
        if (mutated.geometryCompileCount <= stable.geometryCompileCount)
        {
            throw std::runtime_error("Text/border mutation failed to invalidate retained UI.");
        }
        Viewport(960, 540, 1.5);
        auto target = LiteMinecraft::NativeOptions().target;
        target.width = 960;
        target.height = 540;
        bbl::Check(bl_setNativeTarget(engine.state->engine, &target));
        bgfx::SwapChain swapChain{};
        swapChain.width = 960;
        swapChain.height = 540;
        swapChain.formatColor = bgfx::TextureFormat::BGRA8;
        swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
        bgfx::reset(BGFX_RESET_NONE, &swapChain);
        if (!capture.empty())
        {
            std::filesystem::create_directories(capture);
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE,
                                    (capture / "ui-boundary.png").string().c_str());
        }
        submit(1002.0 / 60.0);
        if (!capture.empty())
        {
            bbl::Check(bl_waitForGpuIdle(engine.state->engine));
            for (uint32_t frame = 0; frame < 4; ++frame)
            {
                bgfx::frame();
            }
            WriteDom(capture / "dom.txt");
        }
        Dispose();
        std::printf("Native UI boundary: 1000 unchanged frames retained, input/text/border/"
                    "scale/DPI passed; this is NOT original Minecraft parity.\n");
        return 0;
    }
}
