#include "PresentProbe.h"

#include <windows.h>
#include <psapi.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace LiteScenesProbe
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        using GetProc = FARPROC(WINAPI*)(HMODULE, LPCSTR);
        using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
        using CreateFactory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
        using CreateSwapChain = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*,
                                                            DXGI_SWAP_CHAIN_DESC*,
                                                            IDXGISwapChain**);
        using CreateForHwnd = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
                                                          const DXGI_SWAP_CHAIN_DESC1*,
                                                          const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
                                                          IDXGIOutput*, IDXGISwapChain1**);
        using CreateForComposition = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*,
                                                                 const DXGI_SWAP_CHAIN_DESC1*,
                                                                 IDXGIOutput*, IDXGISwapChain1**);
        using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        using Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT,
                                                     const DXGI_PRESENT_PARAMETERS*);

        struct Patch
        {
            void** slot{};
            void* original{};
        };

        struct Event
        {
            int64_t begin{};
            int64_t end{};
            uintptr_t swapchain{};
            uint32_t syncInterval{};
            uint32_t flags{};
            int32_t result{};
            bool sourceFrame{};
        };

        GetProc realGetProc{::GetProcAddress};
        CreateFactory realFactory{};
        CreateFactory realFactory1{};
        CreateFactory2 realFactory2{};
        Patch patches[512]{};
        std::atomic<size_t> patchCount{};
        Event events[16384]{};
        std::atomic<bool> ready[16384]{};
        std::atomic<uint32_t> eventCount{};
        uint32_t diagnosticFlags{};
        bool installed{};
        std::atomic<bool> active{};
        std::atomic<bool> sourceFrame{};
        std::atomic<bool> observationFailed{};
        std::mutex mutex;
        int64_t frequency{};
        int64_t processStartQpc{};
        int64_t calibrationUncertaintyTicks{};

        int64_t Counter()
        {
            LARGE_INTEGER value{};
            QueryPerformanceCounter(&value);
            return value.QuadPart;
        }

        void* Original(void** slot)
        {
            for (size_t i = 0; i < patchCount.load(std::memory_order_acquire); ++i)
            {
                if (patches[i].slot == slot)
                {
                    return patches[i].original;
                }
            }
            std::_Exit(91);
        }

        void PatchSlot(void** slot, void* replacement)
        {
            if (*slot == replacement)
            {
                return;
            }
            const auto index = patchCount.load();
            if (index == std::size(patches))
            {
                throw std::runtime_error("Present probe patch capacity exceeded");
            }
            DWORD protection{};
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection))
            {
                throw std::runtime_error("Present probe cannot protect pointer");
            }
            patches[index] = {slot, *slot};
            patchCount.store(index + 1, std::memory_order_release);
            InterlockedExchangePointer(slot, replacement);
            DWORD ignored{};
            VirtualProtect(slot, sizeof(void*), protection, &ignored);
        }

        HRESULT STDMETHODCALLTYPE OnPresent(IDXGISwapChain* chain, UINT sync, UINT flags)
        {
            auto** table = *reinterpret_cast<void***>(chain);
            const auto original = reinterpret_cast<Present>(Original(table + 8));
            const auto begin = Counter();
            const auto isSource = sourceFrame.load();
            const HRESULT result = original(chain, sync, flags);
            if (active && !(flags & DXGI_PRESENT_TEST))
            {
                const auto index = eventCount.fetch_add(1);
                if (index < std::size(events))
                {
                    events[index] = {begin,   Counter(), reinterpret_cast<uintptr_t>(chain),
                                     sync,    flags,     result,
                                     isSource};
                    ready[index].store(true, std::memory_order_release);
                }
            }
            return result;
        }

        HRESULT STDMETHODCALLTYPE OnPresent1(IDXGISwapChain1* chain, UINT sync, UINT flags,
                                             const DXGI_PRESENT_PARAMETERS* parameters)
        {
            auto** table = *reinterpret_cast<void***>(chain);
            const auto original = reinterpret_cast<Present1>(Original(table + 22));
            const auto begin = Counter();
            const auto isSource = sourceFrame.load();
            const HRESULT result = original(chain, sync, flags, parameters);
            if (active && !(flags & DXGI_PRESENT_TEST))
            {
                const auto index = eventCount.fetch_add(1);
                if (index < std::size(events))
                {
                    events[index] = {begin,   Counter(), reinterpret_cast<uintptr_t>(chain),
                                     sync,    flags,     result,
                                     isSource};
                    ready[index].store(true, std::memory_order_release);
                }
            }
            return result;
        }

        void ObserveChain(IDXGISwapChain* chain) noexcept
        {
            try
            {
                std::lock_guard lock(mutex);
                auto** table = *reinterpret_cast<void***>(chain);
                PatchSlot(table + 8, reinterpret_cast<void*>(OnPresent));
                ComPtr<IDXGISwapChain1> version1;
                if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&version1))))
                {
                    auto** table1 = *reinterpret_cast<void***>(version1.Get());
                    PatchSlot(table1 + 22, reinterpret_cast<void*>(OnPresent1));
                }
            }
            catch (...)
            {
                observationFailed = true;
            }
        }

        HRESULT STDMETHODCALLTYPE OnSwapChain(IDXGIFactory* factory, IUnknown* device,
                                              DXGI_SWAP_CHAIN_DESC* descriptor,
                                              IDXGISwapChain** chain)
        {
            auto** table = *reinterpret_cast<void***>(factory);
            const auto original = reinterpret_cast<CreateSwapChain>(Original(table + 10));
            const HRESULT result = original(factory, device, descriptor, chain);
            if (SUCCEEDED(result))
            {
                ObserveChain(*chain);
            }
            return result;
        }

        HRESULT STDMETHODCALLTYPE OnForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND window,
                                            const DXGI_SWAP_CHAIN_DESC1* descriptor,
                                            const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
                                            IDXGIOutput* output, IDXGISwapChain1** chain)
        {
            auto** table = *reinterpret_cast<void***>(factory);
            const auto original = reinterpret_cast<CreateForHwnd>(Original(table + 15));
            const HRESULT result =
                original(factory, device, window, descriptor, fullscreen, output, chain);
            if (SUCCEEDED(result))
            {
                ObserveChain(*chain);
            }
            return result;
        }

        HRESULT STDMETHODCALLTYPE OnComposition(IDXGIFactory2* factory, IUnknown* device,
                                                const DXGI_SWAP_CHAIN_DESC1* descriptor,
                                                IDXGIOutput* output, IDXGISwapChain1** chain)
        {
            auto** table = *reinterpret_cast<void***>(factory);
            const auto original = reinterpret_cast<CreateForComposition>(Original(table + 24));
            const HRESULT result = original(factory, device, descriptor, output, chain);
            if (SUCCEEDED(result))
            {
                ObserveChain(*chain);
            }
            return result;
        }

        void ObserveFactory(IUnknown* factory) noexcept
        {
            try
            {
                std::lock_guard lock(mutex);
                ComPtr<IDXGIFactory> base;
                if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&base))))
                {
                    auto** table = *reinterpret_cast<void***>(base.Get());
                    PatchSlot(table + 10, reinterpret_cast<void*>(OnSwapChain));
                }
                ComPtr<IDXGIFactory2> version2;
                if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&version2))))
                {
                    auto** table = *reinterpret_cast<void***>(version2.Get());
                    PatchSlot(table + 15, reinterpret_cast<void*>(OnForHwnd));
                    PatchSlot(table + 24, reinterpret_cast<void*>(OnComposition));
                }
            }
            catch (...)
            {
                observationFailed = true;
            }
        }

        HRESULT WINAPI OnFactory(REFIID iid, void** output)
        {
            const HRESULT result = realFactory(iid, output);
            if (SUCCEEDED(result))
            {
                ObserveFactory(static_cast<IUnknown*>(*output));
            }
            return result;
        }

        HRESULT WINAPI OnFactory1(REFIID iid, void** output)
        {
            const HRESULT result = realFactory1(iid, output);
            if (SUCCEEDED(result))
            {
                ObserveFactory(static_cast<IUnknown*>(*output));
            }
            return result;
        }

        HRESULT WINAPI OnFactory2(UINT flags, REFIID iid, void** output)
        {
            const HRESULT result = realFactory2(flags, iid, output);
            if (SUCCEEDED(result))
            {
                ObserveFactory(static_cast<IUnknown*>(*output));
            }
            return result;
        }

        void* Replacement(const char* name)
        {
            if (!std::strcmp(name, "CreateDXGIFactory"))
            {
                return reinterpret_cast<void*>(OnFactory);
            }
            if (!std::strcmp(name, "CreateDXGIFactory1"))
            {
                return reinterpret_cast<void*>(OnFactory1);
            }
            if (!std::strcmp(name, "CreateDXGIFactory2"))
            {
                return reinterpret_cast<void*>(OnFactory2);
            }
            return nullptr;
        }

        FARPROC WINAPI OnGetProc(HMODULE module, LPCSTR name)
        {
            const auto result = realGetProc(module, name);
            if (reinterpret_cast<uintptr_t>(name) > 65535)
            {
                if (const auto replacement = Replacement(name))
                {
                    return reinterpret_cast<FARPROC>(replacement);
                }
            }
            return result;
        }

        void ObserveImports(HMODULE module)
        {
            auto* base = reinterpret_cast<uint8_t*>(module);
            const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return;
            }
            const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (!directory.VirtualAddress)
            {
                return;
            }
            auto* imports =
                reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
            for (; imports->Name; ++imports)
            {
                if (!imports->OriginalFirstThunk)
                {
                    continue;
                }
                auto* names =
                    reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
                auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++addresses)
                {
                    if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                    {
                        continue;
                    }
                    const auto* entry =
                        reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                    const auto* name = reinterpret_cast<const char*>(entry->Name);
                    void* replacement = !std::strcmp(name, "GetProcAddress")
                                            ? reinterpret_cast<void*>(OnGetProc)
                                            : Replacement(name);
                    if (replacement)
                    {
                        PatchSlot(reinterpret_cast<void**>(&addresses->u1.Function), replacement);
                    }
                }
            }
        }

    }

    void Install(uint32_t flags)
    {
        if (installed)
        {
            throw std::runtime_error("Present probe is already installed");
        }
        diagnosticFlags = flags;
        LARGE_INTEGER qpf{};
        QueryPerformanceFrequency(&qpf);
        frequency = qpf.QuadPart;
        FILETIME created{};
        FILETIME exited{};
        FILETIME kernel{};
        FILETIME user{};
        if (!frequency || !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        {
            throw std::runtime_error("Present probe cannot calibrate process creation");
        }
        FILETIME now{};
        const auto before = Counter();
        GetSystemTimePreciseAsFileTime(&now);
        const auto after = Counter();
        ULARGE_INTEGER startFile{};
        startFile.LowPart = created.dwLowDateTime;
        startFile.HighPart = created.dwHighDateTime;
        ULARGE_INTEGER nowFile{};
        nowFile.LowPart = now.dwLowDateTime;
        nowFile.HighPart = now.dwHighDateTime;
        processStartQpc = (before + after) / 2 -
                          static_cast<int64_t>((nowFile.QuadPart - startFile.QuadPart) *
                                               static_cast<long double>(frequency) / 10000000);
        calibrationUncertaintyTicks = after - before;
        const auto dxgi = LoadLibraryW(L"dxgi.dll");
        if (!dxgi)
        {
            throw std::runtime_error("Present probe cannot load DXGI");
        }
        realFactory = reinterpret_cast<CreateFactory>(realGetProc(dxgi, "CreateDXGIFactory"));
        realFactory1 = reinterpret_cast<CreateFactory>(realGetProc(dxgi, "CreateDXGIFactory1"));
        realFactory2 = reinterpret_cast<CreateFactory2>(realGetProc(dxgi, "CreateDXGIFactory2"));
        if (!realFactory || !realFactory1 || !realFactory2)
        {
            throw std::runtime_error("Present probe cannot resolve factory entry points");
        }
        HMODULE modules[512]{};
        DWORD bytes{};
        if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes) ||
            bytes > sizeof(modules))
        {
            throw std::runtime_error("Present probe cannot enumerate its own modules");
        }
        for (size_t i = 0; i < bytes / sizeof(HMODULE); ++i)
        {
            ObserveImports(modules[i]);
        }
        installed = true;
        active = true;
    }

    void MarkSourceFrame()
    {
        sourceFrame = true;
    }

    void Write(const std::filesystem::path& path)
    {
        active = false;
        const auto count = eventCount.load();
        if (observationFailed || !count || count > std::size(events))
        {
            throw std::runtime_error("Present observer failed, saw no Present, or overflowed");
        }
        while (!ready[0].load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        std::ofstream file(path);
        file << std::setprecision(17) << "{\"processStartQpc\":" << processStartQpc
             << ",\"qpcFrequency\":" << frequency
             << ",\"calibrationUncertaintyTicks\":" << calibrationUncertaintyTicks
             << ",\"diagnosticFlags\":" << diagnosticFlags << ",\"firstPresentMs\":"
             << double(events[0].end - processStartQpc) * 1000 / double(frequency)
             << ",\"presents\":[";
        for (uint32_t i = 0; i < count; ++i)
        {
            while (!ready[i].load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            const auto& event = events[i];
            if (i)
            {
                file << ',';
            }
            file << "{\"beginQpc\":" << event.begin << ",\"endQpc\":" << event.end
                 << ",\"syncInterval\":" << event.syncInterval << ",\"flags\":" << event.flags
                 << ",\"result\":" << event.result
                 << ",\"sourceFrame\":" << (event.sourceFrame ? "true" : "false") << '}';
        }
        file << "]}\n";
        if (!file)
        {
            throw std::runtime_error("Present observation output failed");
        }
    }

    void Shutdown()
    {
        active = false;
    }

    void WriteBaseline(const double* samples, size_t count)
    {
        char path[4096]{};
        if (GetEnvironmentVariableA("LITE_M0_PRESENT_OUT", path, sizeof(path)) > 0)
        {
            Write(std::filesystem::path(path));
        }
        if (GetEnvironmentVariableA("LITE_M0_FRAME_OUT", path, sizeof(path)) > 0)
        {
            std::ofstream file(path);
            file << std::setprecision(17)
                 << "{\"scope\":\"complete-host-including-events\","
                    "\"warmup\":120,\"measure\":"
                 << count << ",\"samplesMs\":[";
            for (size_t i = 0; i < count; ++i)
            {
                if (i)
                {
                    file << ',';
                }
                file << samples[i];
            }
            file << "]}\n";
            if (!file)
            {
                throw std::runtime_error("Baseline frame output failed");
            }
        }
    }
}
