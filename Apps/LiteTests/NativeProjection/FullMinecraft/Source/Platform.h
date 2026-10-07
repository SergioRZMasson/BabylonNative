#pragma once

#include "C99Client.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace LiteMinecraft
{
    void InitializePlatform(HWND window, bool interactive);
    void DisposePlatform();
    void DispatchPlatformEvents();
    void Replay(uint32_t frame);
    void ConfigureSaveLoadTest(const std::filesystem::path& path);
    void SaveLoadReplay(uint32_t frame);
    void TickPlatform(double deltaMs);
    void PresentHUD();
    uint64_t InputEvents();
    uint32_t FileDialogs();
    bool PlatformClosed();
    bool WritePNG(const std::filesystem::path& path, uint32_t width, uint32_t height,
                  uint32_t pitch, const uint8_t* bgra, bool flip);
    void CaptureHUD(const std::filesystem::path& path);
    LRESULT PlatformMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
