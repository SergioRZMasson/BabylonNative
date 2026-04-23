#include "BabylonRenderer.h"
#include "OrbitCamera.h"
#include "MathTypes.h"

#include <Windows.h>
#include <Windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <shobjidl.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

// --- Global State ---
static HWND g_hWnd = nullptr;
static ComPtr<ID3D11Device> g_device;
static ComPtr<ID3D11DeviceContext> g_deviceContext;
static ComPtr<IDXGISwapChain1> g_swapChain;
static ComPtr<ID3D11Texture2D> g_renderTarget;

static std::optional<IntegrationTest::BabylonRenderer> g_renderer;
static IntegrationTest::OrbitCamera g_camera;
static IntegrationTest::Matrix4 g_sceneTransform = IntegrationTest::Matrix4::Identity();

static bool g_dirty = false;
static bool g_animated = false;
static bool g_modelLoaded = false;
static uint32_t g_width = 1280;
static uint32_t g_height = 853;

// Animation state
static int g_animIndex = 0;
static float g_animDuration = 0.0f;
static std::chrono::high_resolution_clock::time_point g_animStartTime;

// Mouse tracking
static bool g_leftDragging = false;
static bool g_rightDragging = false;
static int g_lastMouseX = 0;
static int g_lastMouseY = 0;

// Forward declarations
ATOM RegisterWindowClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitD3D11(HWND hWnd, uint32_t width, uint32_t height);
void ResizeD3D11(uint32_t width, uint32_t height);
void PresentFrame();
void OpenFileDialog();
void LoadModelFromFile(const std::wstring& filePath);
std::vector<char> LoadBinaryFile(const std::string& path);
std::string WideToUtf8(const std::wstring& wide);

// --- Entry Point ---
int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR /*lpCmdLine*/,
    _In_ int nCmdShow)
{
    // Initialize COM for file dialogs
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return 1;

    RegisterWindowClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        CoUninitialize();
        return 1;
    }

    MSG msg{};

    while (msg.message != WM_QUIT)
    {
        if (g_animated && g_modelLoaded)
        {
            // Continuous render mode for animated models
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                    goto exit;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            // Compute animation progress
            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - g_animStartTime).count();
            float progress = (g_animDuration > 0.0f)
                ? std::fmod(elapsed, g_animDuration) / g_animDuration
                : 0.0f;

            auto cam = g_camera.GetCameraTransform();
            g_renderer->Render(cam, g_sceneTransform, g_animIndex, progress);
            PresentFrame();
        }
        else
        {
            // Drain all pending messages
            if (g_dirty)
            {
                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                        goto exit;
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
            else
            {
                // Block until a message arrives
                if (!GetMessage(&msg, nullptr, 0, 0))
                    break;
                TranslateMessage(&msg);
                DispatchMessage(&msg);

                // Drain additional queued messages
                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                        goto exit;
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }

            // Render on demand for static models
            if (g_dirty && g_modelLoaded)
            {
                auto cam = g_camera.GetCameraTransform();
                g_renderer->Render(cam, g_sceneTransform);
                PresentFrame();
                g_dirty = false;
            }
        }
    }

exit:
    g_renderer.reset();
    g_renderTarget.Reset();
    g_swapChain.Reset();
    g_deviceContext.Reset();
    g_device.Reset();
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

// --- Window Registration ---
ATOM RegisterWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"IntegrationTestClass";
    return RegisterClassExW(&wcex);
}

// --- Window Creation ---
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    HWND hWnd = CreateWindowW(
        L"IntegrationTestClass",
        L"Babylon Native Integration Test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, g_width, g_height,
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
        return FALSE;

    g_hWnd = hWnd;

    if (!InitD3D11(hWnd, g_width, g_height))
        return FALSE;

    // Initialize Babylon renderer
    g_renderer.emplace();

    // Find the bundle path relative to the executable
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path bundlePath = std::filesystem::path(exePath).parent_path() / "Scripts" / "BabylonInterop.bundle.js";

    try
    {
        g_renderer->Init(bundlePath.string(), g_width, g_height);
    }
    catch (const std::exception& e)
    {
        std::wstring msg = L"Failed to initialize Babylon renderer:\n";
        msg += std::wstring(e.what(), e.what() + strlen(e.what()));
        MessageBoxW(hWnd, msg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    EnableMouseInPointer(TRUE);

    return TRUE;
}

// --- D3D11 Initialization ---
bool InitD3D11(HWND hWnd, uint32_t width, uint32_t height)
{
    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel;
    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createFlags, nullptr, 0,
        D3D11_SDK_VERSION,
        g_device.GetAddressOf(), &featureLevel, g_deviceContext.GetAddressOf());

    if (FAILED(hr))
    {
        // Retry without debug layer
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0,
            D3D11_SDK_VERSION,
            g_device.GetAddressOf(), &featureLevel, g_deviceContext.GetAddressOf());
    }

    if (FAILED(hr))
        return false;

    // Get DXGI factory
    ComPtr<IDXGIDevice> dxgiDevice;
    g_device.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(adapter.GetAddressOf());

    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    hr = factory->CreateSwapChainForHwnd(
        g_device.Get(), hWnd, &swapChainDesc, nullptr, nullptr,
        g_swapChain.GetAddressOf());

    if (FAILED(hr))
        return false;

    // Create intermediate render target texture
    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    hr = g_device->CreateTexture2D(&texDesc, nullptr, g_renderTarget.GetAddressOf());
    return SUCCEEDED(hr);
}

// --- Resize ---
void ResizeD3D11(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;
    if (width == g_width && height == g_height)
        return;

    g_width = width;
    g_height = height;

    g_renderTarget.Reset();
    g_deviceContext->ClearState();
    g_deviceContext->Flush();

    HRESULT hr = g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
        return;

    // Recreate render target texture
    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    g_device->CreateTexture2D(&texDesc, nullptr, g_renderTarget.GetAddressOf());

    // Rebind in Babylon renderer
    if (g_renderer && g_modelLoaded)
    {
        g_renderer->BindRenderTarget(g_renderTarget);
    }

    g_dirty = true;
}

// --- Present ---
void PresentFrame()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    g_deviceContext->CopyResource(backBuffer.Get(), g_renderTarget.Get());
    g_swapChain->Present(1, 0);
}

// --- File Loading ---
std::vector<char> LoadBinaryFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return {};

    auto size = file.tellg();
    if (size <= 0)
        return {};

    std::vector<char> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(data.data(), size);
    return data;
}

std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
    return result;
}

// --- File Open Dialog ---
void OpenFileDialog()
{
    ComPtr<IFileOpenDialog> pFileOpen;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(pFileOpen.GetAddressOf()));
    if (FAILED(hr))
        return;

    COMDLG_FILTERSPEC fileTypes[] = {
        {L"glTF Binary Files", L"*.glb"},
        {L"All Files", L"*.*"},
    };
    pFileOpen->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes);
    pFileOpen->SetTitle(L"Select a 3D Model");

    hr = pFileOpen->Show(g_hWnd);
    if (FAILED(hr))
        return;

    ComPtr<IShellItem> pItem;
    hr = pFileOpen->GetResult(pItem.GetAddressOf());
    if (FAILED(hr))
        return;

    PWSTR pszFilePath;
    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
    if (SUCCEEDED(hr))
    {
        std::wstring filePath(pszFilePath);
        CoTaskMemFree(pszFilePath);
        LoadModelFromFile(filePath);
    }
}

// --- Load Model ---
void LoadModelFromFile(const std::wstring& filePath)
{
    std::string modelPath = WideToUtf8(filePath);

    // Find environment.env relative to the executable
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    auto envPath = std::filesystem::path(exePath).parent_path() / "assets" / "environment.env";

    auto modelData = LoadBinaryFile(modelPath);
    auto environmentData = LoadBinaryFile(envPath.string());

    if (modelData.empty())
    {
        MessageBoxW(g_hWnd, L"Failed to load model file.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    if (environmentData.empty())
    {
        MessageBoxW(g_hWnd, L"Failed to load environment.env.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Release previous model if loaded
    if (g_modelLoaded)
    {
        g_renderer->ReleaseModel();
        g_modelLoaded = false;
        g_animated = false;
    }

    try
    {
        // Load the model into Babylon
        g_renderer->LoadModel(std::move(modelData), std::move(environmentData));

        // Bind the render target
        g_renderer->BindRenderTarget(g_renderTarget);

        // Frame the camera on the model
        float center[3], extents[3];
        g_renderer->GetBoundingBox(center, extents);
        g_camera.FrameModel(center, extents);

        // Check for animations
        auto animInfos = g_renderer->GetAnimationInfos();
        g_animated = false;
        g_animDuration = 0.0f;

        for (size_t i = 0; i < animInfos.size(); i++)
        {
            if (animInfos[i].Duration > 0.0f)
            {
                g_animated = true;
                g_animIndex = static_cast<int>(i);
                g_animDuration = animInfos[i].Duration;
                g_animStartTime = std::chrono::high_resolution_clock::now();
                break;
            }
        }

        g_modelLoaded = true;
        g_dirty = true;

        // Update window title with model name
        std::filesystem::path p(filePath);
        std::wstring title = L"Babylon Native - " + p.filename().wstring();
        if (g_animated)
            title += L" [Animated]";
        SetWindowTextW(g_hWnd, title.c_str());
    }
    catch (const std::exception& e)
    {
        std::wstring msg = L"Failed to load model:\n";
        msg += std::wstring(e.what(), e.what() + strlen(e.what()));
        MessageBoxW(g_hWnd, msg.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }
}

// --- Window Procedure ---
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_SIZE:
        {
            auto width = static_cast<uint32_t>(LOWORD(lParam));
            auto height = static_cast<uint32_t>(HIWORD(lParam));
            if (width > 0 && height > 0)
                ResizeD3D11(width, height);
            break;
        }

        case WM_KEYDOWN:
        {
            if (wParam == 'O')
            {
                OpenFileDialog();
            }
            else if (wParam == VK_F12 && g_renderer)
            {
                g_renderer->GetRenderDocCapture().RequestCapture();
                g_dirty = true;
            }
            break;
        }

        case WM_POINTERDOWN:
        {
            auto pointerId = GET_POINTERID_WPARAM(wParam);
            POINTER_INFO info;
            POINT origin{0, 0};

            if (GetPointerInfo(pointerId, &info) && ClientToScreen(hWnd, &origin))
            {
                int x = GET_X_LPARAM(lParam) - origin.x;
                int y = GET_Y_LPARAM(lParam) - origin.y;
                g_lastMouseX = x;
                g_lastMouseY = y;

                if (info.ButtonChangeType == POINTER_CHANGE_FIRSTBUTTON_DOWN)
                    g_leftDragging = true;
                else if (info.ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_DOWN)
                    g_rightDragging = true;
            }
            break;
        }

        case WM_POINTERUPDATE:
        {
            auto pointerId = GET_POINTERID_WPARAM(wParam);
            POINTER_INFO info;
            POINT origin{0, 0};

            if (GetPointerInfo(pointerId, &info) && ClientToScreen(hWnd, &origin))
            {
                int x = GET_X_LPARAM(lParam) - origin.x;
                int y = GET_Y_LPARAM(lParam) - origin.y;
                int deltaX = x - g_lastMouseX;
                int deltaY = y - g_lastMouseY;
                g_lastMouseX = x;
                g_lastMouseY = y;

                if (g_leftDragging)
                {
                    g_camera.Orbit(deltaX * 0.005f, deltaY * 0.005f);
                    g_dirty = true;
                }
                else if (g_rightDragging)
                {
                    g_camera.Pan(static_cast<float>(-deltaX), static_cast<float>(deltaY));
                    g_dirty = true;
                }

                // Process button changes during move
                if (info.ButtonChangeType == POINTER_CHANGE_FIRSTBUTTON_DOWN)
                    g_leftDragging = true;
                else if (info.ButtonChangeType == POINTER_CHANGE_FIRSTBUTTON_UP)
                    g_leftDragging = false;
                else if (info.ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_DOWN)
                    g_rightDragging = true;
                else if (info.ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_UP)
                    g_rightDragging = false;
            }
            break;
        }

        case WM_POINTERUP:
        {
            auto pointerId = GET_POINTERID_WPARAM(wParam);
            POINTER_INFO info;

            if (GetPointerInfo(pointerId, &info))
            {
                if (info.ButtonChangeType == POINTER_CHANGE_FIRSTBUTTON_UP)
                    g_leftDragging = false;
                else if (info.ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_UP)
                    g_rightDragging = false;
            }
            break;
        }

        case WM_POINTERWHEEL:
        {
            float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam));
            g_camera.Zoom(delta);
            g_dirty = true;
            break;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
            g_dirty = true;
            break;
        }

        case WM_DESTROY:
        {
            if (g_modelLoaded)
            {
                g_renderer->ReleaseModel();
                g_modelLoaded = false;
            }
            g_renderer.reset();
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
