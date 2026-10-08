#pragma once

#include <napi/napi.h>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <memory>
#include <vector>

namespace LitePlayground
{
    struct NativeViewport
    {
        uint32_t width{};
        uint32_t height{};
        double density{1};
        bool changed{};
    };

    class NativePlatform
    {
    public:
        NativePlatform(HWND window, uint32_t width, uint32_t height, bool retainedUi = false,
                       bool boundedInput = false, bool headless = false);
        ~NativePlatform();
        void Initialize(Napi::Env env, const std::filesystem::path& assets);
        void DisposeOnJsThread();
        void DispatchEvents(Napi::Env env);
        void Replay(Napi::Env env, uint32_t frame);
        void QueueWindowReplay(uint32_t frame);
        void QueueUiValidation(uint32_t frame);
        NativeViewport TakeViewport();
        void ConfigureFileDialogTest(const std::filesystem::path& path);
        bool Message(UINT message, WPARAM wParam, LPARAM lParam);
        bool Closed() const;
        size_t AssetFailures() const;
        bool CaptureHUD(const std::filesystem::path& path) const;
        uint64_t NativeInputEvents() const;
        uint32_t FileDialogsCompleted() const;
        uint32_t FileSelectionsCompleted() const;
        static bool WritePNG(const std::filesystem::path& path, uint32_t width, uint32_t height,
            uint32_t pitch, const uint8_t* bgra, bool flip);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
