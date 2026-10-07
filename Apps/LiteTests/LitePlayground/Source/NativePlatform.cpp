#include "NativePlatform.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <commdlg.h>
#include <dlgs.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace LitePlayground
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        std::wstring Wide(const std::string& value)
        {
            const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
            std::wstring result(static_cast<size_t>(count), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
            return result;
        }

        std::string Utf8(const std::wstring& value)
        {
            const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<size_t>(count), '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
            return result;
        }

        std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                throw std::runtime_error("Cannot read native platform file: " + path.string());
            }
            const auto size = stream.tellg();
            std::vector<uint8_t> result(static_cast<size_t>(size));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(result.data()), size);
            if (!stream)
            {
                throw std::runtime_error("Native platform file read failed.");
            }
            return result;
        }

        std::string Key(UINT key)
        {
            if (key >= 'A' && key <= 'Z')
            {
                return std::string(1, static_cast<char>(key + ('a' - 'A')));
            }
            if (key >= '0' && key <= '9')
            {
                return std::string(1, static_cast<char>(key));
            }
            switch (key)
            {
                case VK_SPACE:
                    return " ";
                case VK_TAB:
                    return "Tab";
                case VK_SHIFT:
                    return "Shift";
                case VK_CONTROL:
                    return "Control";
                case VK_ESCAPE:
                    return "Escape";
                case VK_F3:
                    return "F3";
                case VK_UP:
                    return "ArrowUp";
                case VK_DOWN:
                    return "ArrowDown";
                default:
                    return "";
            }
        }

        std::string Code(UINT key)
        {
            if (key >= 'A' && key <= 'Z')
            {
                return "Key" + std::string(1, static_cast<char>(key));
            }
            if (key >= '0' && key <= '9')
            {
                return "Digit" + std::string(1, static_cast<char>(key));
            }
            if (key == VK_SPACE)
                return "Space";
            if (key == VK_SHIFT)
                return "ShiftLeft";
            if (key == VK_CONTROL)
                return "ControlLeft";
            return Key(key);
        }
    }

    struct NativePlatform::Impl
    {
        struct Event
        {
            std::string type;
            std::string key;
            std::string code;
            int button{};
            int movementX{};
            int movementY{};
            int deltaY{};
            bool shift{};
            bool control{};
            bool repeat{};
        };

        HWND window{};
        HWND overlay{};
        uint32_t width{};
        uint32_t height{};
        std::atomic<bool> locked{};
        std::atomic<bool> closed{};
        uint64_t nativeInputEvents{};
        bool controlDown{};
        bool shiftDown{};
        uint32_t fileDialogsCompleted{};
        std::wstring dialogTestPath;
        size_t assetFailures{};
        std::filesystem::path assets;
        std::mutex eventsMutex;
        std::vector<Event> events;
        std::vector<uint8_t> hudPixels;
        ComPtr<IWICImagingFactory> imaging;
        bool ownsComInitialization{};

        static UINT_PTR CALLBACK TestFileDialog(HWND window, UINT message, WPARAM, LPARAM data)
        {
            if (message == WM_INITDIALOG)
            {
                const auto* options = reinterpret_cast<const OPENFILENAMEW*>(data);
                SetWindowLongPtrW(window, GWLP_USERDATA, options->lCustData);
                SetTimer(window, 1, 100, nullptr);
            }
            else if (message == WM_TIMER)
            {
                KillTimer(window, 1);
                const auto* state = reinterpret_cast<const Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
                HWND dialog = GetParent(window);
                SendMessageW(dialog, CDM_SETCONTROLTEXT, cmb13, reinterpret_cast<LPARAM>(state->dialogTestPath.c_str()));
                PostMessageW(dialog, WM_COMMAND, IDOK, 0);
            }
            return 0;
        }

        std::filesystem::path Resolve(const std::string& url) const
        {
            const auto marker = url.find("/minecraft/voxelpack/");
            if (marker != std::string::npos)
            {
                return assets / "minecraft" / "voxelpack" / url.substr(marker + std::string("/minecraft/voxelpack/").size());
            }
            if (url.starts_with("app:///"))
            {
                return assets / url.substr(7);
            }
            return std::filesystem::path(Wide(url));
        }

        void Push(Event event)
        {
            std::lock_guard lock(eventsMutex);
            events.push_back(std::move(event));
        }

        Napi::Object Decode(Napi::Env env, Napi::Uint8Array bytes)
        {
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> converter;
            HRESULT result = imaging->CreateStream(&stream);
            if (SUCCEEDED(result))
                result = stream->InitializeFromMemory(bytes.Data(), static_cast<DWORD>(bytes.ElementLength()));
            if (SUCCEEDED(result))
                result = imaging->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
            if (SUCCEEDED(result))
                result = decoder->GetFrame(0, &frame);
            UINT imageWidth{};
            UINT imageHeight{};
            if (SUCCEEDED(result))
                result = frame->GetSize(&imageWidth, &imageHeight);
            if (SUCCEEDED(result))
                result = imaging->CreateFormatConverter(&converter);
            if (SUCCEEDED(result))
                result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
            auto pixels = Napi::Uint8Array::New(env, static_cast<size_t>(imageWidth) * imageHeight * 4);
            if (SUCCEEDED(result))
                result = converter->CopyPixels(nullptr, imageWidth * 4, static_cast<UINT>(pixels.ElementLength()), pixels.Data());
            if (FAILED(result))
            {
                ++assetFailures;
                throw Napi::Error::New(env, "WIC native PNG decode failed.");
            }
            auto image = Napi::Object::New(env);
            image.Set("width", static_cast<double>(imageWidth));
            image.Set("height", static_cast<double>(imageHeight));
            image.Set("pixels", pixels);
            return image;
        }

        void Paint(Napi::Array commands)
        {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(width);
            info.bmiHeader.biHeight = -static_cast<LONG>(height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* storage{};
            HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &storage, nullptr, 0);
            auto old = SelectObject(dc, bitmap);
            auto* pixels = static_cast<uint8_t*>(storage);
            std::memset(pixels, 0, static_cast<size_t>(width) * height * 4);
            const auto blend = [&](int x, int y, const std::array<double, 4>& color) {
                if (x < 0 || y < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height))
                    return;
                auto* destination = pixels + (static_cast<size_t>(y) * width + x) * 4;
                const double alpha = std::clamp(color[3], 0.0, 1.0);
                for (size_t channel = 0; channel < 3; ++channel)
                {
                    destination[channel] = static_cast<uint8_t>(std::clamp(color[2 - channel] * alpha + destination[channel] * (1 - alpha), 0.0, 255.0));
                }
                destination[3] = static_cast<uint8_t>(std::clamp(alpha * 255 + destination[3] * (1 - alpha), 0.0, 255.0));
            };
            for (uint32_t index = 0; index < commands.Length(); ++index)
            {
                const auto command = commands.Get(index).As<Napi::Object>();
                const auto kind = command.Get("kind").As<Napi::String>().Utf8Value();
                const int x = command.Get("x").As<Napi::Number>().Int32Value();
                const int y = command.Get("y").As<Napi::Number>().Int32Value();
                const int w = command.Get("width").As<Napi::Number>().Int32Value();
                const int h = command.Get("height").As<Napi::Number>().Int32Value();
                std::array<double, 4> color{};
                if (command.Has("color"))
                {
                    const auto values = command.Get("color").As<Napi::Array>();
                    for (uint32_t i = 0; i < 4; ++i)
                        color[i] = values.Get(i).As<Napi::Number>().DoubleValue();
                }
                const double opacity = command.Has("opacity") ? command.Get("opacity").As<Napi::Number>().DoubleValue() : 1;
                color[3] *= opacity;
                if (kind == "radial" || kind == "insetShadow")
                {
                    std::array<double, 4> outer{};
                    double blur{};
                    double spread{};
                    if (kind == "radial")
                    {
                        const auto values = command.Get("outer").As<Napi::Array>();
                        for (uint32_t i = 0; i < 4; ++i)
                            outer[i] = values.Get(i).As<Napi::Number>().DoubleValue();
                        outer[3] *= opacity;
                    }
                    else
                    {
                        blur = command.Get("blur").As<Napi::Number>().DoubleValue();
                        spread = command.Get("spread").As<Napi::Number>().DoubleValue();
                    }
                    for (int row = std::max(0, y); row < std::min(static_cast<int>(height), y + h); ++row)
                    {
                        for (int col = std::max(0, x); col < std::min(static_cast<int>(width), x + w); ++col)
                        {
                            auto tint = color;
                            if (kind == "radial")
                            {
                                const double nx = (col - x - w / 2.0) / (w / 2.0);
                                const double ny = (row - y - h / 2.0) / (h / 2.0);
                                const double amount = std::clamp(std::sqrt((nx * nx + ny * ny) / 2), 0.0, 1.0);
                                for (uint32_t i = 0; i < 4; ++i)
                                    tint[i] += (outer[i] - tint[i]) * amount;
                            }
                            else
                            {
                                const double edge = std::min({col - x, row - y, x + w - col - 1, y + h - row - 1});
                                tint[3] *= std::clamp((spread + blur / 2 - edge) / std::max(1.0, blur), 0.0, 1.0);
                            }
                            blend(col, row, tint);
                        }
                    }
                }
                else if (kind == "rect" || kind == "border")
                {
                    const int border = kind == "border" ? command.Get("thickness").As<Napi::Number>().Int32Value() : 0;
                    for (int row = std::max(0, y); row < std::min(static_cast<int>(height), y + h); ++row)
                    {
                        for (int col = std::max(0, x); col < std::min(static_cast<int>(width), x + w); ++col)
                        {
                            if (!border || row < y + border || row >= y + h - border || col < x + border || col >= x + w - border)
                                blend(col, row, color);
                        }
                    }
                }
                else if (kind == "image")
                {
                    const auto image = command.Get("image").As<Napi::Object>();
                    const int imageWidth = image.Get("width").As<Napi::Number>().Int32Value();
                    const int imageHeight = image.Get("height").As<Napi::Number>().Int32Value();
                    const auto bytes = image.Get("pixels").As<Napi::Uint8Array>();
                    for (int row = 0; row < h; ++row)
                    {
                        for (int col = 0; col < w; ++col)
                        {
                            const auto offset = (static_cast<size_t>(row * imageHeight / h) * imageWidth + col * imageWidth / w) * 4;
                            blend(x + col, y + row, {static_cast<double>(bytes[offset]), static_cast<double>(bytes[offset + 1]), static_cast<double>(bytes[offset + 2]), bytes[offset + 3] / 255.0 * opacity});
                        }
                    }
                }
                else if (kind == "text")
                {
                    const int size = command.Get("size").As<Napi::Number>().Int32Value();
                    const auto text = Wide(command.Get("text").As<Napi::String>().Utf8Value());
                    const auto fontSpec = command.Get("font").As<Napi::String>().Utf8Value();
                    HDC maskDC = CreateCompatibleDC(nullptr);
                    void* maskStorage{};
                    HBITMAP mask = CreateDIBSection(maskDC, &info, DIB_RGB_COLORS, &maskStorage, nullptr, 0);
                    auto oldMask = SelectObject(maskDC, mask);
                    std::memset(maskStorage, 0, static_cast<size_t>(width) * height * 4);
                    HFONT font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                        fontSpec.find("monospace") != std::string::npos ? L"Consolas" : L"Segoe UI");
                    auto oldFont = SelectObject(maskDC, font);
                    SetTextColor(maskDC, RGB(255, 255, 255));
                    SetBkMode(maskDC, TRANSPARENT);
                    RECT rectangle{x, y, x + w, y + h};
                    DrawTextW(maskDC, text.c_str(), static_cast<int>(text.size()), &rectangle, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
                    const auto* maskPixels = static_cast<const uint8_t*>(maskStorage);
                    for (int row = std::max(0, y); row < std::min(static_cast<int>(height), y + h); ++row)
                    {
                        for (int col = std::max(0, x); col < std::min(static_cast<int>(width), x + w); ++col)
                        {
                            const double coverage = maskPixels[(static_cast<size_t>(row) * width + col) * 4] / 255.0;
                            auto tint = color;
                            tint[3] *= coverage;
                            if (coverage)
                                blend(col, row, tint);
                        }
                    }
                    SelectObject(maskDC, oldFont);
                    DeleteObject(font);
                    SelectObject(maskDC, oldMask);
                    DeleteObject(mask);
                    DeleteDC(maskDC);
                }
            }
            POINT destination{};
            ClientToScreen(window, &destination);
            POINT source{};
            SIZE dimensions{static_cast<LONG>(width), static_cast<LONG>(height)};
            BLENDFUNCTION function{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            const bool updated = UpdateLayeredWindow(overlay, nullptr, &destination, &dimensions, dc, &source, 0, &function, ULW_ALPHA) != FALSE;
            hudPixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
            SelectObject(dc, old);
            DeleteObject(bitmap);
            DeleteDC(dc);
            if (!updated)
            {
                throw Napi::Error::New(commands.Env(), "Native Win32 HUD presentation failed.");
            }
        }
    };

    NativePlatform::NativePlatform(HWND window, uint32_t width, uint32_t height)
        : m_impl(std::make_unique<Impl>())
    {
        m_impl->window = window;
        m_impl->width = width;
        m_impl->height = height;
        m_impl->overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, L"STATIC", L"Babylon Lite native DOM HUD",
            WS_POPUP | (IsWindowVisible(window) ? WS_VISIBLE : 0), 0, 0, static_cast<int>(width), static_cast<int>(height),
            window, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    NativePlatform::~NativePlatform()
    {
        ClipCursor(nullptr);
        if (m_impl->overlay)
            DestroyWindow(m_impl->overlay);
    }

    void NativePlatform::Initialize(Napi::Env env, const std::filesystem::path& assets)
    {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_impl->ownsComInitialization = SUCCEEDED(initialized);
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        {
            throw Napi::Error::New(env, "Native platform COM initialization failed.");
        }
        m_impl->assets = assets;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_impl->imaging))))
        {
            throw Napi::Error::New(env, "WIC initialization failed.");
        }
        auto api = Napi::Object::New(env);
        api.Set("width", static_cast<double>(m_impl->width));
        api.Set("height", static_cast<double>(m_impl->height));
        api.Set("read", Napi::Function::New(env, [this](const Napi::CallbackInfo& info) {
            try
            {
                const auto bytes = ReadFile(m_impl->Resolve(info[0].As<Napi::String>().Utf8Value()));
                auto result = Napi::Uint8Array::New(info.Env(), bytes.size());
                std::memcpy(result.Data(), bytes.data(), bytes.size());
                return Napi::Value(result);
            }
            catch (const std::exception& error)
            {
                ++m_impl->assetFailures;
                Napi::Error::New(info.Env(), error.what()).ThrowAsJavaScriptException();
                return info.Env().Undefined();
            }
        }));
        api.Set("decodeImage", Napi::Function::New(env, [this](const Napi::CallbackInfo& info) { return m_impl->Decode(info.Env(), info[0].As<Napi::Uint8Array>()); }));
        api.Set("encode", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
            const auto text = info[0].As<Napi::String>().Utf8Value();
            auto bytes = Napi::Uint8Array::New(info.Env(), text.size());
            std::memcpy(bytes.Data(), text.data(), text.size());
            return bytes;
        }));
        api.Set("decodeText", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
            const auto bytes = info[0].As<Napi::Uint8Array>();
            return Napi::String::New(info.Env(), reinterpret_cast<const char*>(bytes.Data()), bytes.ElementLength());
        }));
        api.Set("write", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
            const auto path = Wide(info[0].As<Napi::String>().Utf8Value());
            const auto bytes = info[1].As<Napi::Uint8Array>();
            std::ofstream stream(std::filesystem::path(path), std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.Data()), static_cast<std::streamsize>(bytes.ElementLength()));
            if (!stream)
                throw Napi::Error::New(info.Env(), "Native save file write failed.");
        }));
        api.Set("pickFile", Napi::Function::New(env, [this](const Napi::CallbackInfo& info) {
            const bool save = info[0].As<Napi::Boolean>().Value();
            const bool wasLocked = m_impl->locked.exchange(false);
            ClipCursor(nullptr);
            std::array<wchar_t, 32768> path{};
            const auto suggested = Wide(info[1].As<Napi::String>().Utf8Value());
            std::copy(suggested.begin(), suggested.end(), path.begin());
            if (!m_impl->dialogTestPath.empty())
            {
                std::copy(m_impl->dialogTestPath.begin(), m_impl->dialogTestPath.end(), path.begin());
            }
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = m_impl->window;
            dialog.lpstrFile = path.data();
            dialog.nMaxFile = static_cast<DWORD>(path.size());
            dialog.lpstrFilter = L"Voxel world JSON\0*.json\0All files\0*.*\0";
            dialog.lpstrDefExt = L"json";
            dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
            if (!m_impl->dialogTestPath.empty())
            {
                dialog.Flags |= OFN_ENABLEHOOK;
                dialog.lpfnHook = Impl::TestFileDialog;
                dialog.lCustData = reinterpret_cast<LPARAM>(m_impl.get());
            }
            const bool selected = save ? GetSaveFileNameW(&dialog) != FALSE : GetOpenFileNameW(&dialog) != FALSE;
            if (selected)
            {
                ++m_impl->fileDialogsCompleted;
            }
            if (wasLocked)
            {
                RECT rectangle{};
                GetClientRect(m_impl->window, &rectangle);
                MapWindowPoints(m_impl->window, nullptr, reinterpret_cast<POINT*>(&rectangle), 2);
                ClipCursor(&rectangle);
                m_impl->locked = true;
            }
            return Napi::String::New(info.Env(), selected ? Utf8(path.data()) : "");
        }));
        api.Set("pointerLock", Napi::Function::New(env, [this](const Napi::CallbackInfo& info) {
            m_impl->locked = info[0].As<Napi::Boolean>().Value();
            if (m_impl->locked)
            {
                RECT rectangle{};
                GetClientRect(m_impl->window, &rectangle);
                MapWindowPoints(m_impl->window, nullptr, reinterpret_cast<POINT*>(&rectangle), 2);
                ClipCursor(&rectangle);
                RAWINPUTDEVICE device{0x01, 0x02, RIDEV_INPUTSINK, m_impl->window};
                RegisterRawInputDevices(&device, 1, sizeof(device));
                while (ShowCursor(FALSE) >= 0)
                {
                }
            }
            else
            {
                ClipCursor(nullptr);
                while (ShowCursor(TRUE) < 0)
                {
                }
            }
        }));
        api.Set("hud", Napi::Function::New(env, [this](const Napi::CallbackInfo& info) { m_impl->Paint(info[0].As<Napi::Array>()); }));
        env.Global().Set("_litePlatform", api);
    }

    bool NativePlatform::Message(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_SETCURSOR && m_impl->locked)
        {
            SetCursor(nullptr);
            return true;
        }
        if (message == WM_KILLFOCUS && m_impl->locked)
        {
            m_impl->Push({"pointerunlock"});
        }
        if (message == WM_INPUT || message == WM_KEYDOWN || message == WM_KEYUP ||
            message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN ||
            message == WM_RBUTTONUP || message == WM_MOUSEWHEEL)
        {
            ++m_impl->nativeInputEvents;
        }
        if (message == WM_CLOSE)
        {
            m_impl->closed = true;
            return true;
        }
        if (message == WM_INPUT && m_impl->locked)
        {
            UINT size{};
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            std::vector<uint8_t> bytes(size);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, bytes.data(), &size, sizeof(RAWINPUTHEADER)) == size)
            {
                const auto& input = *reinterpret_cast<const RAWINPUT*>(bytes.data());
                if (input.header.dwType == RIM_TYPEMOUSE)
                {
                    Impl::Event event;
                    event.type = "mousemove";
                    event.movementX = input.data.mouse.lLastX;
                    event.movementY = input.data.mouse.lLastY;
                    m_impl->Push(std::move(event));
                }
            }
        }
        if (message == WM_KEYDOWN || message == WM_KEYUP)
        {
            if (wParam == VK_CONTROL)
                m_impl->controlDown = message == WM_KEYDOWN;
            if (wParam == VK_SHIFT)
                m_impl->shiftDown = message == WM_KEYDOWN;
            if (wParam == VK_ESCAPE)
            {
                m_impl->Push({"pointerunlock"});
                return true;
            }
            Impl::Event event;
            event.type = message == WM_KEYDOWN ? "keydown" : "keyup";
            event.key = Key(static_cast<UINT>(wParam));
            event.code = Code(static_cast<UINT>(wParam));
            event.shift = m_impl->shiftDown || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            event.control = m_impl->controlDown || (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            event.repeat = (lParam & (1 << 30)) != 0;
            m_impl->Push(std::move(event));
        }
        if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONUP)
        {
            Impl::Event event;
            event.type = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ? "mousedown" : "mouseup";
            event.button = message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ? 2 : 0;
            m_impl->Push(event);
            if (message == WM_LBUTTONUP)
            {
                event.type = "click";
                m_impl->Push(event);
            }
        }
        if (message == WM_MOUSEWHEEL)
        {
            Impl::Event event;
            event.type = "wheel";
            event.deltaY = -GET_WHEEL_DELTA_WPARAM(wParam);
            m_impl->Push(std::move(event));
        }
        return false;
    }

    void NativePlatform::DispatchEvents(Napi::Env env)
    {
        std::vector<Impl::Event> events;
        {
            std::lock_guard lock(m_impl->eventsMutex);
            events.swap(m_impl->events);
        }

        const auto dispatch = env.Global().Get("_platformDispatch");
        if (!dispatch.IsFunction())
            return;
        for (const auto& event : events)
        {
            auto data = Napi::Object::New(env);
            data.Set("type", event.type);
            data.Set("key", event.key);
            data.Set("code", event.code);
            data.Set("button", static_cast<double>(event.button));
            data.Set("movementX", static_cast<double>(event.movementX));
            data.Set("movementY", static_cast<double>(event.movementY));
            data.Set("deltaY", static_cast<double>(event.deltaY));
            data.Set("ctrlKey", event.control);
            data.Set("shiftKey", event.shift);
            data.Set("metaKey", false);
            data.Set("repeat", event.repeat);
            dispatch.As<Napi::Function>().Call({data});
        }
    }

    void NativePlatform::DisposeOnJsThread()
    {
        m_impl->imaging.Reset();
        if (m_impl->locked.exchange(false))
        {
            ClipCursor(nullptr);
            while (ShowCursor(TRUE) < 0)
            {
            }
        }
        if (m_impl->ownsComInitialization)
        {
            CoUninitialize();
            m_impl->ownsComInitialization = false;
        }
    }

    void NativePlatform::Replay(Napi::Env env, uint32_t frame)
    {
        if (frame == 1)
            m_impl->Push({"click"});
        if (frame == 2)
            m_impl->Push({"keydown", "F3", "F3"});
        if (frame == 3)
            m_impl->Push({"keydown", "w", "KeyW"});
        if (frame == 80)
            m_impl->Push({"keyup", "w", "KeyW"});
        if (frame >= 30 && frame < 60)
        {
            Impl::Event event;
            event.type = "mousemove";
            event.movementX = 2;
            m_impl->Push(event);
        }
        if (frame == 90)
            m_impl->Push({"mousedown", "", "", 0});
        if (frame == 120)
            m_impl->Push({"keydown", "2", "Digit2"});
        if (frame == 150)
            m_impl->Push({"mousedown", "", "", 2});
        DispatchEvents(env);
    }

    void NativePlatform::QueueWindowReplay(uint32_t frame)
    {
        const auto key = [&](UINT code, bool down) { PostMessageW(m_impl->window, down ? WM_KEYDOWN : WM_KEYUP, code, 0); };
        if (frame == 1)
        {
            PostMessageW(m_impl->window, WM_LBUTTONDOWN, 0, MAKELPARAM(640, 360));
            PostMessageW(m_impl->window, WM_LBUTTONUP, 0, MAKELPARAM(640, 360));
        }
        if (frame == 2)
            key(VK_F3, true);
        if (frame == 3)
            key('W', true);
        if (frame == 12)
            key(VK_SHIFT, true);
        if (frame == 20)
            key(VK_SPACE, true);
        if (frame == 21)
            key(VK_SPACE, false);
        if (frame == 45)
            key(VK_SHIFT, false);
        if (frame == 80)
            key('W', false);
        if (frame == 90)
            PostMessageW(m_impl->window, WM_LBUTTONDOWN, 0, MAKELPARAM(640, 360));
        if (frame == 91)
            PostMessageW(m_impl->window, WM_LBUTTONUP, 0, MAKELPARAM(640, 360));
        if (frame == 120)
            key('2', true);
        if (frame == 150)
            PostMessageW(m_impl->window, WM_RBUTTONDOWN, 0, MAKELPARAM(640, 360));
        if (frame == 151)
            PostMessageW(m_impl->window, WM_RBUTTONUP, 0, MAKELPARAM(640, 360));
        if (frame == 165)
            PostMessageW(m_impl->window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        if (!m_impl->dialogTestPath.empty() && (frame == 185 || frame == 215))
        {
            key(VK_CONTROL, true);
            key(frame == 185 ? 'S' : 'O', true);
            key(frame == 185 ? 'S' : 'O', false);
            key(VK_CONTROL, false);
        }
    }

    void NativePlatform::ConfigureFileDialogTest(const std::filesystem::path& path)
    {
        m_impl->dialogTestPath = std::filesystem::absolute(path).wstring();
    }

    bool NativePlatform::Closed() const { return m_impl->closed; }
    size_t NativePlatform::AssetFailures() const { return m_impl->assetFailures; }
    uint64_t NativePlatform::NativeInputEvents() const { return m_impl->nativeInputEvents; }
    uint32_t NativePlatform::FileDialogsCompleted() const { return m_impl->fileDialogsCompleted; }

    bool NativePlatform::CaptureHUD(const std::filesystem::path& path) const
    {
        return !m_impl->hudPixels.empty() && WritePNG(path, m_impl->width, m_impl->height,
                                                 m_impl->width * 4, m_impl->hudPixels.data(), false);
    }

    bool NativePlatform::WritePNG(const std::filesystem::path& path, uint32_t width, uint32_t height, uint32_t pitch, const uint8_t* bgra, bool flip)
    {
        struct Apartment
        {
            HRESULT status{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
            ~Apartment()
            {
                if (SUCCEEDED(status))
                {
                    CoUninitialize();
                }
            }
        } apartment;
        if (FAILED(apartment.status) && apartment.status != RPC_E_CHANGED_MODE)
        {
            return false;
        }
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> properties;
        HRESULT status = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(status))
            status = factory->CreateStream(&stream);
        if (SUCCEEDED(status))
            status = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(status))
            status = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (SUCCEEDED(status))
            status = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (SUCCEEDED(status))
            status = encoder->CreateNewFrame(&frame, &properties);
        if (SUCCEEDED(status))
            status = frame->Initialize(properties.Get());
        if (SUCCEEDED(status))
            status = frame->SetSize(width, height);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(status))
            status = frame->SetPixelFormat(&format);
        std::vector<uint8_t> rows(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y)
        {
            std::memcpy(rows.data() + static_cast<size_t>(y) * width * 4, bgra + static_cast<size_t>(flip ? height - y - 1 : y) * pitch, width * 4);
        }
        if (SUCCEEDED(status))
            status = frame->WritePixels(height, width * 4, static_cast<UINT>(rows.size()), rows.data());
        if (SUCCEEDED(status))
            status = frame->Commit();
        if (SUCCEEDED(status))
            status = encoder->Commit();
        return SUCCEEDED(status);
    }
}
