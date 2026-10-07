#include "Platform.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <commdlg.h>
#include <dlgs.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <regex>

namespace
{
    using Microsoft::WRL::ComPtr;

    struct Element
    {
        std::string tag;
        std::string text;
        std::string css;
        std::map<std::string, std::string> styles;
        std::map<std::string, std::string> attributes;
        std::vector<uint32_t> children;
        std::vector<bbl::js::NativeFile> files;
        bbl::js::Callback<void()> change;
        std::string downloadName;
        uint32_t url{};
    };

    template<class T> struct Listener
    {
        bbl::DomEventTarget target;
        std::string type;
        bbl::js::Callback<void(const T&)> callback;
        bool once;
    };

    struct Event
    {
        std::string type;
        bbl::PlatformKeyboardEvent keyboard;
        bbl::PlatformMouseEvent pointer;
        bool key{};
    };

    struct Timer
    {
        double due;
        bbl::js::Callback<void()> callback;
    };

    HWND s_window{};
    HWND s_overlay{};
    bool s_interactive{};
    bool s_closed{};
    bool s_locked{};
    uint64_t s_events{};
    uint32_t s_dialogs{};
    double s_time{};
    std::chrono::steady_clock::time_point s_clockStart;
    uint64_t s_timerId{};
    std::map<uint64_t, Timer> s_timers;
    std::vector<Listener<bbl::PlatformKeyboardEvent>> s_keyboard;
    std::vector<Listener<bbl::PlatformMouseEvent>> s_pointer;
    std::vector<bbl::js::Callback<void()>> s_lock;
    std::vector<Event> s_queue;
    std::vector<Element> s_elements(1);
    std::map<uint32_t, std::string> s_urls;
    uint32_t s_urlId{};
    std::vector<uint8_t> s_hud;
    std::map<std::string, std::vector<uint8_t>> s_icons;
    POINT s_previous{};
    bool s_hasPrevious{};
    std::filesystem::path s_dialogTestPath;
    std::weak_ptr<bbl::EngineState> s_inputEngine;

    double Clock()
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                         s_clockStart)
            .count();
    }

    UINT_PTR CALLBACK DialogHook(HWND dialog, UINT message, WPARAM, LPARAM)
    {
        if (message == WM_INITDIALOG && !s_dialogTestPath.empty())
        {
            SetTimer(dialog, 1, 100, nullptr);
        }
        if (message == WM_TIMER && !s_dialogTestPath.empty())
        {
            const HWND parent = GetParent(dialog);
            const auto path = s_dialogTestPath.wstring();
            SendMessageW(parent, CDM_SETCONTROLTEXT, cmb13, reinterpret_cast<LPARAM>(path.c_str()));
            PostMessageW(parent, WM_COMMAND, IDOK, 0);
            KillTimer(dialog, 1);
        }
        return 0;
    }

    std::wstring Wide(const std::string& text)
    {
        const int size =
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                            size);
        return result;
    }

    std::string Utf8(const std::wstring& text)
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                            size, nullptr, nullptr);
        return result;
    }

    std::string Dialog(bool save, const std::string& name)
    {
        std::array<wchar_t, 32768> file{};
        const auto wide = s_dialogTestPath.empty() ? Wide(name) : s_dialogTestPath.wstring();
        std::copy_n(wide.data(), std::min(wide.size(), file.size() - 1), file.data());
        OPENFILENAMEW options{};
        options.lStructSize = sizeof(options);
        options.hwndOwner = s_window;
        options.lpstrFile = file.data();
        options.nMaxFile = static_cast<DWORD>(file.size());
        options.lpstrFilter = L"Voxel worlds\0*.json\0All files\0*.*\0";
        options.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        options.Flags |= save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
        if (!s_dialogTestPath.empty())
        {
            options.Flags |= OFN_ENABLEHOOK;
            options.lpfnHook = DialogHook;
        }
        const bool restoreLock = s_locked && s_interactive;
        if (restoreLock)
        {
            ClipCursor(nullptr);
            ReleaseCapture();
            ShowCursor(TRUE);
        }
        const bool success =
            save ? GetSaveFileNameW(&options) != FALSE : GetOpenFileNameW(&options) != FALSE;
        if (restoreLock)
        {
            RECT rectangle{};
            GetClientRect(s_window, &rectangle);
            MapWindowPoints(s_window, nullptr, reinterpret_cast<POINT*>(&rectangle), 2);
            ClipCursor(&rectangle);
            SetCapture(s_window);
            ShowCursor(FALSE);
        }
        if (!success)
        {
            return {};
        }
        ++s_dialogs;
        return Utf8(file.data());
    }

    std::string Key(WPARAM value)
    {
        if (value >= 'A' && value <= 'Z')
        {
            return std::string(1, static_cast<char>(value + ('a' - 'A')));
        }
        if (value >= '0' && value <= '9')
        {
            return std::string(1, static_cast<char>(value));
        }
        switch (value)
        {
            case VK_SPACE:
                return " ";
            case VK_TAB:
                return "Tab";
            case VK_SHIFT:
                return "Shift";
            case VK_ESCAPE:
                return "Escape";
            case VK_F3:
                return "F3";
            default:
                return "";
        }
    }

    std::string Code(WPARAM value)
    {
        if (value >= 'A' && value <= 'Z')
        {
            return "Key" + std::string(1, static_cast<char>(value));
        }
        if (value >= '0' && value <= '9')
        {
            return "Digit" + std::string(1, static_cast<char>(value));
        }
        if (value == VK_SPACE)
        {
            return "Space";
        }
        if (value == VK_SHIFT)
        {
            return "ShiftLeft";
        }
        return Key(value);
    }

    template<class T>
    void Dispatch(std::vector<Listener<T>>& listeners, const std::string& type, const T& event)
    {
        const auto snapshot = listeners;
        for (const auto& listener : snapshot)
        {
            if (listener.type == type)
            {
                listener.callback(event);
            }
        }
        std::erase_if(listeners,
                      [&](const auto& listener) { return listener.once && listener.type == type; });
    }

    std::vector<uint8_t> DecodeIcon(const std::string& source)
    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        auto file = std::filesystem::path(LITE_MINECRAFT_ORIGINAL_ASSETS) /
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
            result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0,
                                           WICBitmapPaletteTypeCustom);
        }
        UINT width{};
        UINT height{};
        if (SUCCEEDED(result))
        {
            result = converter->GetSize(&width, &height);
        }
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        if (SUCCEEDED(result))
        {
            result = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()),
                                           pixels.data());
        }
        if (FAILED(result) || width != height || width == 0)
        {
            throw std::runtime_error("Native HUD icon decode failed: " + file.string());
        }
        return pixels;
    }
}

namespace bbl
{
    void PlatformKeyboardEvent::prevent_default() const
    {
        default_prevented = true;
    }

    void PlatformMouseEvent::prevent_default() const
    {
        default_prevented = true;
    }

    void on_dom_keyboard(Engine&, DomEventTarget target, const std::string& type, size_t,
                         js::Callback<void(const PlatformKeyboardEvent&)> callback, bool, bool once,
                         bool)
    {
        s_keyboard.push_back({target, type, std::move(callback), once});
    }

    void on_dom_pointer(Engine&, DomEventTarget target, const std::string& type, size_t,
                        js::Callback<void(const PlatformMouseEvent&)> callback, bool, bool once,
                        bool)
    {
        s_pointer.push_back({target, type, std::move(callback), once});
    }

    void on_pointer_lock_change(Engine& engine, size_t, js::Callback<void()> callback)
    {
        s_inputEngine = engine.state;
        s_lock.push_back(std::move(callback));
    }

    void request_pointer_lock(Engine& engine)
    {
        s_locked = true;
        engine.state->pointerLocked = true;
        if (s_interactive)
        {
            SetCapture(s_window);
            RECT rectangle{};
            GetClientRect(s_window, &rectangle);
            MapWindowPoints(s_window, nullptr, reinterpret_cast<POINT*>(&rectangle), 2);
            ClipCursor(&rectangle);
            ShowCursor(FALSE);
        }
        for (auto& callback : s_lock)
        {
            callback();
        }
    }

    double set_timeout(Engine&, js::Callback<void()> callback, double delayMs)
    {
        const auto id = ++s_timerId;
        s_timers.emplace(id, Timer{Clock() + std::max(0.0, delayMs), std::move(callback)});
        return static_cast<double>(id);
    }

    void clear_timeout(Engine&, double token)
    {
        s_timers.erase(static_cast<uint64_t>(token));
    }

    UiElementHandle ui_create_element(Engine&, const std::string& tag)
    {
        Element element{};
        element.tag = tag;
        s_elements.push_back(std::move(element));
        return static_cast<uint32_t>(s_elements.size() - 1);
    }

    UiElementHandle ui_document_root(Engine&, UiDocumentPart)
    {
        return 0;
    }

    void ui_append_child(Engine&, UiElementHandle parent, UiElementHandle child)
    {
        s_elements.at(parent).children.push_back(child);
    }

    void ui_append_to_root(Engine& engine, UiElementHandle child)
    {
        ui_append_child(engine, 0, child);
    }

    void ui_canvas_set_width(Engine&, UiElementHandle element, double width)
    {
        s_elements.at(element).attributes["width"] = std::to_string(width);
    }

    void ui_canvas_set_height(Engine&, UiElementHandle element, double height)
    {
        s_elements.at(element).attributes["height"] = std::to_string(height);
    }

    void ui_set_attribute(Engine&, UiElementHandle element, const std::string& name,
                          const std::string& value)
    {
        auto& item = s_elements.at(element);
        item.attributes[name] = value;
        if (name == "style")
        {
            item.css = value;
            const std::regex icon("(?:url|image)\\(\"([^\"]+)\"");
            std::smatch match;
            if (std::regex_search(value, match, icon) && !s_icons.contains(match[1].str()))
            {
                s_icons.emplace(match[1].str(), DecodeIcon(match[1].str()));
            }
        }
    }

    void ui_set_style_property(Engine&, UiElementHandle element, const std::string& name,
                               const std::string& value)
    {
        s_elements.at(element).styles[name] = value;
    }

    void ui_set_inner_rml(Engine&, UiElementHandle element, const std::string& value)
    {
        s_elements.at(element).text = value;
    }

    void ui_set_text(Engine& engine, UiElementHandle element, const std::string& value)
    {
        ui_set_inner_rml(engine, element, value);
    }

    void ui_set_download_name(Engine&, UiElementHandle element, const std::string& value)
    {
        s_elements.at(element).downloadName = value;
    }

    void ui_set_download_url(Engine&, UiElementHandle element, uint32_t url)
    {
        s_elements.at(element).url = url;
    }

    void ui_set_file_accept(Engine& engine, UiElementHandle element, const std::string& value)
    {
        ui_set_attribute(engine, element, "accept", value);
    }

    void ui_set_file_input(Engine& engine, UiElementHandle element)
    {
        ui_set_attribute(engine, element, "type", "file");
    }

    void ui_on_file_change(Engine&, UiElementHandle element, js::Callback<void()> callback)
    {
        s_elements.at(element).change = std::move(callback);
    }

    void ui_click(Engine&, UiElementHandle element)
    {
        auto& item = s_elements.at(element);
        if (item.tag == "a")
        {
            const auto path = Dialog(true, item.downloadName);
            if (!path.empty())
            {
                std::ofstream stream(path, std::ios::binary);
                stream << s_urls.at(item.url);
                if (!stream)
                {
                    throw std::runtime_error("Native save failed.");
                }
            }
        }
        else if (item.tag == "input")
        {
            const auto path = Dialog(false, "");
            item.files.clear();
            if (!path.empty())
            {
                std::ifstream stream(path, std::ios::binary);
                item.files.push_back(std::make_shared<std::string>(
                    std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
            }
            const auto callback = item.change;
            if (callback)
            {
                callback();
            }
        }
    }
}

namespace bbl::js
{
    BlobPart blob_part_string(const std::string& value)
    {
        return {value};
    }

    Blob::Blob(std::initializer_list<BlobPart> parts, const std::string&)
    {
        for (const auto& part : parts)
        {
            bytes += part.value;
        }
    }

    uint32_t create_object_url(Engine&, const Blob& blob)
    {
        const auto id = ++s_urlId;
        s_urls.emplace(id, blob.bytes);
        return id;
    }

    void revoke_object_url(Engine&, uint32_t url)
    {
        s_urls.erase(url);
    }

    std::vector<NativeFile> input_files(Engine&, UiElementHandle element)
    {
        return s_elements.at(element).files;
    }

    NativeFile file_at(const std::vector<NativeFile>& files, uint32_t index)
    {
        if (index < files.size())
        {
            return files[index];
        }
        return {};
    }

    Nullable<std::string> FileReader::result() const
    {
        return state->result;
    }

    void FileReader::set_onload(Callback<void()> callback)
    {
        state->onload = std::move(callback);
    }

    void FileReader::set_onerror(Callback<void()> callback)
    {
        state->onerror = std::move(callback);
    }

    void FileReader::read_as_text(Engine&, const NativeFile& file)
    {
        if (file)
        {
            state->result = *file;
            if (state->onload)
            {
                state->onload();
            }
        }
        else if (state->onerror)
        {
            state->onerror();
        }
    }
}

namespace LiteMinecraft
{
    void InitializePlatform(HWND window, bool interactive)
    {
        s_window = window;
        s_interactive = interactive;
        s_clockStart = std::chrono::steady_clock::now();
        s_hud.resize(1280 * 720 * 4);
        s_overlay = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"",
            WS_POPUP, 0, 0, 1280, 720, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (interactive)
        {
            RAWINPUTDEVICE mouse{1, 2, 0, window};
            RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
        }
    }

    void DisposePlatform()
    {
        s_keyboard.clear();
        s_pointer.clear();
        s_lock.clear();
        s_timers.clear();
        s_elements.clear();
        if (s_locked && s_interactive)
        {
            ClipCursor(nullptr);
            ReleaseCapture();
            ShowCursor(TRUE);
        }
        DestroyWindow(s_overlay);
    }

    bool PlatformClosed()
    {
        return s_closed;
    }

    uint64_t InputEvents()
    {
        return s_events;
    }

    uint32_t FileDialogs()
    {
        return s_dialogs;
    }

    LRESULT PlatformMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_CLOSE)
        {
            s_closed = true;
            return 0;
        }
        if (!s_interactive)
        {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        Event event{};
        if (message == WM_INPUT && s_locked)
        {
            UINT size{};
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size,
                            sizeof(RAWINPUTHEADER));
            std::vector<uint8_t> bytes(size);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, bytes.data(), &size,
                                sizeof(RAWINPUTHEADER)) == size)
            {
                const auto& input = *reinterpret_cast<const RAWINPUT*>(bytes.data());
                if (input.header.dwType == RIM_TYPEMOUSE)
                {
                    event.type = "mousemove";
                    event.pointer.movement_x = input.data.mouse.lLastX;
                    event.pointer.movement_y = input.data.mouse.lLastY;
                    s_queue.push_back(event);
                }
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }
        if (message == WM_KEYDOWN || message == WM_KEYUP)
        {
            event.key = true;
            event.type = message == WM_KEYDOWN ? "keydown" : "keyup";
            event.keyboard.key = Key(wParam);
            event.keyboard.code = Code(wParam);
            event.keyboard.repeat = (lParam & (1ll << 30)) != 0 && message == WM_KEYDOWN;
            event.keyboard.ctrl_key = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            event.keyboard.shift_key = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (wParam == VK_ESCAPE && message == WM_KEYDOWN && s_locked)
            {
                s_locked = false;
                ClipCursor(nullptr);
                ReleaseCapture();
                ShowCursor(TRUE);
                if (const auto engine = s_inputEngine.lock())
                {
                    engine->pointerLocked = false;
                }
                for (const auto& callback : s_lock)
                {
                    callback();
                }
            }
        }
        else if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                 message == WM_LBUTTONUP || message == WM_RBUTTONUP)
        {
            event.type =
                message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ? "mousedown" : "mouseup";
            event.pointer.button = message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ? 2 : 0;
        }
        else if (message == WM_MOUSEWHEEL)
        {
            event.type = "wheel";
            event.pointer.delta_y = -GET_WHEEL_DELTA_WPARAM(wParam);
        }
        else if (message == WM_MOUSEMOVE && !s_locked)
        {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (s_hasPrevious)
            {
                event.type = "mousemove";
                event.pointer.movement_x = point.x - s_previous.x;
                event.pointer.movement_y = point.y - s_previous.y;
            }
            s_previous = point;
            s_hasPrevious = true;
        }
        if (!event.type.empty())
        {
            s_queue.push_back(event);
            if (message == WM_LBUTTONUP)
            {
                event.type = "click";
                s_queue.push_back(event);
            }
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void DispatchPlatformEvents()
    {
        auto events = std::move(s_queue);
        s_queue.clear();
        for (auto& event : events)
        {
            ++s_events;
            auto state = std::make_shared<bbl::DomEventState>();
            state->type = event.type;
            state->target =
                event.key ? bbl::DomEventTarget::window() : bbl::DomEventTarget::canvas();
            if (event.key)
            {
                event.keyboard.dom = state;
                Dispatch(s_keyboard, event.type, event.keyboard);
            }
            else
            {
                event.pointer.dom = state;
                Dispatch(s_pointer, event.type, event.pointer);
            }
        }
    }

    void Replay(uint32_t frame)
    {
        const auto key =
            [](const std::string& type, const std::string& value, const std::string& code)
        {
            Event event{};
            event.key = true;
            event.type = type;
            event.keyboard.key = value;
            event.keyboard.code = code;
            s_queue.push_back(event);
        };
        if (frame == 1)
        {
            Event event{};
            event.type = "click";
            s_queue.push_back(event);
        }
        if (frame == 2)
        {
            key("keydown", "F3", "F3");
        }
        if (frame == 3)
        {
            key("keydown", "w", "KeyW");
        }
        if (frame == 80)
        {
            key("keyup", "w", "KeyW");
        }
        if (frame >= 30 && frame < 60)
        {
            Event event{};
            event.type = "mousemove";
            event.pointer.movement_x = 2;
            s_queue.push_back(event);
        }
        if (frame == 90 || frame == 150)
        {
            Event event{};
            event.type = "mousedown";
            event.pointer.button = frame == 90 ? 0 : 2;
            s_queue.push_back(event);
        }
        if (frame == 120)
        {
            key("keydown", "2", "Digit2");
        }
    }

    void ConfigureSaveLoadTest(const std::filesystem::path& path)
    {
        s_dialogTestPath = std::filesystem::absolute(path);
    }

    void SaveLoadReplay(uint32_t frame)
    {
        if (frame == 185 || frame == 215)
        {
            Event event{};
            event.key = true;
            event.type = "keydown";
            event.keyboard.ctrl_key = true;
            event.keyboard.key = frame == 185 ? "s" : "o";
            event.keyboard.code = frame == 185 ? "KeyS" : "KeyO";
            s_queue.push_back(event);
            event.type = "keyup";
            s_queue.push_back(event);
        }
    }

    void TickPlatform(double)
    {
        s_time = Clock();
        std::vector<bbl::js::Callback<void()>> due;
        for (auto item = s_timers.begin(); item != s_timers.end();)
        {
            if (item->second.due <= s_time)
            {
                due.push_back(item->second.callback);
                item = s_timers.erase(item);
            }
            else
            {
                ++item;
            }
        }
        for (auto& callback : due)
        {
            callback();
        }
    }

    void PresentHUD()
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 1280;
        info.bmiHeader.biHeight = -720;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HDC dc = CreateCompatibleDC(nullptr);
        void* data{};
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        HGDIOBJ previous = SelectObject(dc, bitmap);
        std::memset(data, 0, 1280 * 720 * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        int slot{};
        for (const auto& element : s_elements)
        {
            if ((element.styles.contains("display") && element.styles.at("display") == "none") ||
                (!element.styles.contains("display") &&
                 element.css.find("display:none") != std::string::npos))
            {
                continue;
            }
            if ((element.styles.contains("opacity") && element.styles.at("opacity") == "0") ||
                (!element.styles.contains("opacity") &&
                 element.css.find("opacity:0;") != std::string::npos))
            {
                continue;
            }
            const auto& css = element.css;
            if (css.find("width:22px") != std::string::npos)
            {
                RECT vertical{639, 349, 641, 371};
                RECT horizontal{629, 359, 651, 361};
                FillRect(dc, &vertical, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                FillRect(dc, &horizontal, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            }
            if (css.find("width:50px") != std::string::npos)
            {
                const int x = 350 + slot++ * 58;
                RECT rectangle{x, 645, x + 54, 699};
                FrameRect(dc, &rectangle, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                const std::regex pattern("(?:url|image)\\(\"([^\"]+)\"");
                std::smatch match;
                if (std::regex_search(css, match, pattern))
                {
                    const auto& pixels = s_icons.at(match[1].str());
                    const int side = static_cast<int>(std::sqrt(pixels.size() / 4));
                    BITMAPINFO icon = info;
                    icon.bmiHeader.biWidth = side;
                    icon.bmiHeader.biHeight = -side;
                    SetStretchBltMode(dc, COLORONCOLOR);
                    StretchDIBits(dc, x + 2, 647, 50, 50, 0, 0, side, side, pixels.data(), &icon,
                                  DIB_RGB_COLORS, SRCCOPY);
                }
                if (element.styles.contains("border-color") &&
                    element.styles.at("border-color") == "#fff")
                {
                    RECT selection{x - 2, 643, x + 56, 701};
                    FrameRect(dc, &selection, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                }
                for (const auto child : element.children)
                {
                    const auto label = Wide(s_elements.at(child).text);
                    RECT labelBox{x + 4, 647, x + 20, 664};
                    DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelBox,
                              DT_LEFT);
                }
            }
            if (!element.text.empty())
            {
                RECT rectangle{20, 14, 1220, 52};
                UINT flags = DT_CENTER | DT_WORDBREAK;
                if (css.find("right:10px") != std::string::npos)
                {
                    rectangle = {1170, 10, 1270, 35};
                    flags = DT_RIGHT;
                }
                else if (css.find("white-space:pre") != std::string::npos)
                {
                    rectangle = {8, 8, 800, 250};
                    flags = DT_LEFT;
                }
                else if (css.find("top:64px") != std::string::npos)
                {
                    rectangle = {200, 64, 1080, 95};
                }
                else if (element.tag == "span")
                {
                    continue;
                }
                auto text = Wide(element.text);
                DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rectangle, flags);
            }
        }
        auto* pixels = static_cast<uint8_t*>(data);
        for (size_t index = 0; index < 1280 * 720 * 4; index += 4)
        {
            pixels[index + 3] = pixels[index] || pixels[index + 1] || pixels[index + 2] ? 255 : 0;
        }
        std::memcpy(s_hud.data(), data, s_hud.size());
        if (IsWindowVisible(s_window))
        {
            POINT destination{};
            ClientToScreen(s_window, &destination);
            POINT source{};
            SIZE size{1280, 720};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(s_overlay, nullptr, &destination, &size, dc, &source, 0, &blend,
                                ULW_ALPHA);
            ShowWindow(s_overlay, SW_SHOWNOACTIVATE);
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    bool WritePNG(const std::filesystem::path& path, uint32_t width, uint32_t height,
                  uint32_t pitch, const uint8_t* bgra, bool flip)
    {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&factory));
        std::filesystem::create_directories(path.parent_path());
        if (SUCCEEDED(result))
        {
            result = factory->CreateStream(&stream);
        }
        if (SUCCEEDED(result))
        {
            result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        }
        if (SUCCEEDED(result))
        {
            result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        }
        if (SUCCEEDED(result))
        {
            result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        }
        if (SUCCEEDED(result))
        {
            result = encoder->CreateNewFrame(&frame, nullptr);
        }
        if (SUCCEEDED(result))
        {
            result = frame->Initialize(nullptr);
        }
        if (SUCCEEDED(result))
        {
            result = frame->SetSize(width, height);
        }
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(result))
        {
            result = frame->SetPixelFormat(&format);
        }
        for (uint32_t y = 0; y < height && SUCCEEDED(result); ++y)
        {
            auto* row = const_cast<uint8_t*>(bgra) + (flip ? height - 1 - y : y) * pitch;
            result = frame->WritePixels(1, pitch, pitch, row);
        }
        if (SUCCEEDED(result))
        {
            result = frame->Commit();
        }
        if (SUCCEEDED(result))
        {
            result = encoder->Commit();
        }
        frame.Reset();
        encoder.Reset();
        stream.Reset();
        factory.Reset();
        if (SUCCEEDED(apartment))
        {
            CoUninitialize();
        }
        return SUCCEEDED(result);
    }

    void CaptureHUD(const std::filesystem::path& path)
    {
        if (!WritePNG(path, 1280, 720, 1280 * 4, s_hud.data(), false))
        {
            throw std::runtime_error("Native HUD capture failed.");
        }
    }
}
