#pragma once

// This header belongs to diagnostic hosts, never the LiteLayer public API.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <tlhelp32.h>
#undef near
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace LitePerf
{
    inline bool Flag(const char* name)
    {
        char value[16]{};
        return GetEnvironmentVariableA(name, value, sizeof(value)) == 1 && value[0] == '1';
    }

    inline const bool enabled = Flag("LITE_PERF_PHASES");
    inline const bool hudOff = Flag("LITE_PERF_HUD_OFF");
    inline const bool hudRetained = Flag("LITE_PERF_HUD_RETAINED");
    inline const bool gcOff = Flag("LITE_PERF_GC_OFF");
    inline const bool gpuOff = Flag("LITE_PERF_GPU_OFF");
    inline const bool cachedRegex = Flag("LITE_PERF_CACHED_REGEX");
    inline const bool allocations = Flag("LITE_PERF_ALLOCATIONS");
    inline const bool groupingCache = Flag("LITE_PERF_GROUPING_CACHE");
    inline const bool queue3 = Flag("LITE_PERF_QUEUE3");
    inline const bool singleThread = Flag("LITE_PERF_SINGLE_THREAD");
    inline const bool drawGuard = Flag("LITE_PERF_DRAW_GUARD");
    inline uint64_t drawHash{1469598103934665603ull};
    inline std::vector<uint64_t> drawHashes;
    inline thread_local bool measuring{};
    inline thread_local std::array<double, 32> phases{};
    inline thread_local std::array<uint64_t, 32> counts{};
    inline std::vector<std::array<double, 32>> samples;
    inline std::vector<std::array<uint64_t, 32>> counters;
    inline LARGE_INTEGER frequency{};
    inline int64_t frameStart{};
    inline uint64_t heapNewCount{};
    inline uint64_t heapNewBytes{};
    inline uint64_t heapDeleteCount{};
    inline uint64_t coreAllocCount{};
    inline uint64_t coreAllocBytes{};
    inline uint64_t coreFreeCount{};
    inline uint64_t coreLiveBytes{};
    inline uint64_t corePeakBytes{};
    inline uint64_t beginNewCount{};
    inline uint64_t beginNewBytes{};
    inline uint64_t beginCoreCount{};
    inline uint64_t beginCoreBytes{};
    inline uint64_t beginCoreFree{};
    inline uint64_t beginDeleteCount{};
    inline thread_local uint64_t managedStart{};
    inline uint64_t heapBegin{};
    inline uint64_t heapEnd{};
    inline uint64_t processBegin{};
    inline uint64_t processEnd{};
    inline uint64_t mainBegin{};
    inline uint64_t mainEnd{};
    inline int64_t runBegin{};
    inline int64_t runEnd{};

    enum Phase
    {
        Platform, Audio, BlFrame, BgfxFrame, Hud, Gc, Callbacks,
        SceneSetup, MeshVisit, Grouping, Sorting, DrawEnqueue,
        HudSetup, HudElements, HudAlpha, HudCopy, HudPresent, HudDispose,
        Acquire, RmlUpdate, Synchronize, Encode, Submit, Finish,
        Timers, RmlRender, DriverSubmit, CoreUniformSet,
        RenderThreadWall, WaitRender, WaitSubmit, Total
    };
    enum Count
    {
        NodesBefore, NodesAfter, ManagedNew, GcRuns, GcCollected, GcRoots, GcEdges,
        VectorGets, VectorSets, UniformSets, UniformSlotTests, HudDraws, RegexCompiles,
        TimerCallbacks, PendingTimers, Draws, RenderItems, WorldCalls, WorldRebuilds,
        NewCount, NewBytes, DeleteCount, CoreAllocations, CoreBytes, CoreFrees,
        UploadBytes, RmlTreeChanges, RmlFullProjections, RmlTextUpdates,
        HudMutations, GcTracedNodes, PrimitiveTriangles
    };

    inline int64_t Tick()
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }
    inline double Ms(int64_t ticks)
    {
        return static_cast<double>(ticks) * 1000.0 / frequency.QuadPart;
    }
    inline void Add(unsigned phase, int64_t start)
    {
        if (enabled && measuring)
        {
            phases[phase] += Ms(Tick() - start);
        }
    }
    inline void CountEvent(unsigned count, uint64_t amount = 1)
    {
        if (enabled && measuring)
        {
            counts[count] += amount;
        }
    }
    struct Scope
    {
        unsigned phase;
        int64_t start;
        explicit Scope(unsigned value) : phase(value), start(enabled && measuring ? Tick() : 0) {}
        ~Scope() { if (start) { Add(phase, start); } }
    };

    inline uint64_t FileTicks(FILETIME value)
    {
        return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    }
    inline uint64_t ProcessCpu()
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
        return FileTicks(kernel) + FileTicks(user);
    }
    inline uint64_t MainCpu()
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
        return FileTicks(kernel) + FileTicks(user);
    }
    inline uint64_t PrivateBytes()
    {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
        return memory.PrivateUsage;
    }
    struct Thread
    {
        DWORD id{};
        uint64_t cpu{};
        uint64_t cycles{};
        std::wstring name;
        std::wstring module;
    };
    inline std::vector<Thread> Threads()
    {
        std::vector<Thread> result;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry))
        {
            do
            {
                if (entry.th32OwnerProcessID != GetCurrentProcessId()) { continue; }
                HANDLE handle = OpenThread(THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                    entry.th32ThreadID);
                if (!handle) { continue; }
                FILETIME created{}, exited{}, kernel{}, user{};
                Thread thread{};
                thread.id = entry.th32ThreadID;
                if (GetThreadTimes(handle, &created, &exited, &kernel, &user))
                {
                    thread.cpu = FileTicks(kernel) + FileTicks(user);
                    QueryThreadCycleTime(handle, &thread.cycles);
                    PWSTR description{};
                    if (SUCCEEDED(GetThreadDescription(handle, &description)) && description)
                    {
                        thread.name = description;
                        LocalFree(description);
                    }
                    using QueryThread = LONG (WINAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
                    const auto query = reinterpret_cast<QueryThread>(GetProcAddress(
                        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
                    void* startAddress{};
                    if (query && query(handle, 9, &startAddress, sizeof(startAddress), nullptr) >= 0)
                    {
                        HMODULE module{};
                        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(startAddress), &module))
                        {
                            wchar_t file[32768]{};
                            if (GetModuleFileNameW(module, file, 32768))
                            {
                                thread.module = file;
                                const auto slash = thread.module.find_last_of(L"\\/");
                                if (slash != std::wstring::npos)
                                {
                                    thread.module.erase(0, slash + 1);
                                }
                            }
                        }
                    }
                    result.push_back(std::move(thread));
                }
                CloseHandle(handle);
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return result;
    }
    inline std::vector<Thread> threadsBegin;
    inline std::vector<Thread> threadsEnd;

    inline void Initialize(size_t frames)
    {
        QueryPerformanceFrequency(&frequency);
        if (enabled)
        {
            samples.reserve(frames);
            counters.reserve(frames);
        }
    }
    inline void BeginRun()
    {
        threadsBegin = Threads();
        heapBegin = PrivateBytes();
        processBegin = ProcessCpu();
        mainBegin = MainCpu();
        runBegin = Tick();
    }
    inline void EndRun()
    {
        runEnd = Tick();
        mainEnd = MainCpu();
        processEnd = ProcessCpu();
        heapEnd = PrivateBytes();
        threadsEnd = Threads();
    }
    inline void BeginFrame()
    {
        measuring = true;
        phases.fill(0);
        counts.fill(0);
        beginNewCount = heapNewCount;
        beginNewBytes = heapNewBytes;
        beginDeleteCount = heapDeleteCount;
        beginCoreCount = coreAllocCount;
        beginCoreBytes = coreAllocBytes;
        beginCoreFree = coreFreeCount;
        frameStart = Tick();
    }
    inline void EndFrame(double total)
    {
        phases[Total] = total;
        counts[NewCount] = heapNewCount - beginNewCount;
        counts[NewBytes] = heapNewBytes - beginNewBytes;
        counts[DeleteCount] = heapDeleteCount - beginDeleteCount;
        counts[CoreAllocations] = coreAllocCount - beginCoreCount;
        counts[CoreBytes] = coreAllocBytes - beginCoreBytes;
        counts[CoreFrees] = coreFreeCount - beginCoreFree;
        measuring = false;
        if (enabled)
        {
            samples.push_back(phases);
            counters.push_back(counts);
        }
    }
    inline void Write(const char* host)
    {
        char path[32768]{};
        const DWORD length = GetEnvironmentVariableA("LITE_PERF_OUTPUT", path, sizeof(path));
        if (!length || length >= sizeof(path)) { return; }
        std::ofstream output(path);
        output << std::setprecision(17);
        const char* names[] = {"platform","audio","blFrame","bgfxFrame","hud","gc","callbacks",
            "sceneSetup","meshVisit","grouping","sorting","drawEnqueue","hudSetup","hudElements",
            "hudAlpha","hudCopy","hudPresent","hudDispose","acquire","rmlUpdate","synchronize",
            "encode","submit","finish","timers","rmlRender","driverSubmit","coreUniformSet",
            "renderThreadWall","waitRender","waitSubmit","total"};
        const char* countNames[] = {"nodesBefore","nodesAfter","managedNew","gcRuns","gcCollected",
            "gcRoots","gcEdges","vectorGets","vectorSets","uniformSets","uniformSlotTests",
            "hudDraws","regexCompiles","timerCallbacks","pendingTimers","draws","renderItems",
            "worldCalls","worldRebuilds","newCount","newBytes","deleteCount","coreAllocations",
            "coreBytes","coreFrees","uploadBytes","rmlTreeChanges","rmlFullProjections",
            "rmlTextUpdates","hudMutations","gcTracedNodes","primitiveTriangles"};
        output << "{\"host\":\"" << host << "\",\"phasesEnabled\":" << enabled
            << ",\"hudOff\":" << hudOff << ",\"hudRetained\":" << hudRetained
            << ",\"gcOff\":" << gcOff << ",\"gpuOff\":" << gpuOff
            << ",\"cachedRegex\":" << cachedRegex << ",\"allocationCounting\":" << allocations
            << ",\"groupingCache\":" << groupingCache << ",\"queue3\":" << queue3
            << ",\"singleThread\":" << singleThread
            << ",\"processCpuMs\":" << (processEnd - processBegin) / 10000.0
            << ",\"mainThreadCpuMs\":" << (mainEnd - mainBegin) / 10000.0
            << ",\"measuredWallMs\":" << Ms(runEnd - runBegin)
            << ",\"privateBytesBegin\":" << heapBegin << ",\"privateBytesEnd\":" << heapEnd
            << ",\"coreLiveBytes\":" << coreLiveBytes << ",\"corePeakBytes\":" << corePeakBytes
            << ",\"mainThreadId\":" << GetCurrentThreadId() << ",\"threads\":[";
        bool comma{};
        for (const auto& end : threadsEnd)
        {
            for (const auto& begin : threadsBegin)
            {
                if (begin.id != end.id) { continue; }
                output << (comma ? "," : "") << "{\"id\":" << end.id << ",\"cpuMs\":"
                    << (end.cpu - begin.cpu) / 10000.0 << ",\"cycles\":"
                    << end.cycles - begin.cycles << ",\"name\":\"";
                for (wchar_t ch : end.name)
                {
                    output << (ch >= 32 && ch < 127 && ch != '"' && ch != '\\' ?
                        static_cast<char>(ch) : '_');
                }
                output << "\",\"startModule\":\"";
                for (wchar_t ch : end.module)
                {
                    output << (ch >= 32 && ch < 127 && ch != '"' && ch != '\\' ?
                        static_cast<char>(ch) : '_');
                }
                output << "\"}";
                comma = true;
            }
        }
        output << "],\"phases\":{";
        for (size_t column = 0; column < 32; ++column)
        {
            output << (column ? "," : "") << '"' << names[column] << "\":[";
            for (size_t row = 0; row < samples.size(); ++row)
            {
                output << (row ? "," : "") << samples[row][column];
            }
            output << ']';
        }
        output << "},\"drawStateHashes\":[";
        for (size_t index = 0; index < drawHashes.size(); ++index)
        {
            output << (index ? ",\"" : "\"") << drawHashes[index] << '"';
        }
        output << "],\"counts\":{";
        for (size_t column = 0; column < 32; ++column)
        {
            output << (column ? "," : "") << '"' << countNames[column] << "\":[";
            for (size_t row = 0; row < counters.size(); ++row)
            {
                output << (row ? "," : "") << counters[row][column];
            }
            output << ']';
        }
        output << "}}\n";
        if (!output) { throw std::runtime_error("Performance receipt write failed."); }
    }
}
