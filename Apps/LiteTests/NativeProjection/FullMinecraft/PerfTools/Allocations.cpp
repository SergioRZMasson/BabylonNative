#include "Perf.h"
#include <new>
#include <malloc.h>

extern "C" int64_t lite_perf_tick()
{
    return LitePerf::enabled && LitePerf::measuring ? LitePerf::Tick() : 0;
}
extern "C" void lite_perf_add(unsigned phase, int64_t start)
{
    if (start) { LitePerf::Add(phase, start); }
}
extern "C" void lite_perf_count(unsigned count, uint64_t amount)
{
    LitePerf::CountEvent(count, amount);
}
extern "C" bool lite_perf_grouping_cache()
{
    return LitePerf::groupingCache;
}
extern "C" void lite_perf_guard(const void* bytes, size_t size)
{
    if (!LitePerf::drawGuard) { return; }
    const auto* data = static_cast<const uint8_t*>(bytes);
    for (size_t index = 0; index < size; ++index)
    {
        LitePerf::drawHash = (LitePerf::drawHash ^ data[index]) * 1099511628211ull;
    }
}

void* operator new(size_t size)
{
    void* result = std::malloc(size ? size : 1);
    if (!result) { throw std::bad_alloc(); }
    if (LitePerf::allocations && LitePerf::measuring)
    {
        ++LitePerf::heapNewCount;
        LitePerf::heapNewBytes += size;
    }
    return result;
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept
{
    if (memory && LitePerf::allocations && LitePerf::measuring)
    {
        ++LitePerf::heapDeleteCount;
    }
    std::free(memory);
}
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, size_t) noexcept { ::operator delete(memory); }
void* operator new(size_t size, std::align_val_t alignment)
{
    void* result = _aligned_malloc(size ? size : 1, static_cast<size_t>(alignment));
    if (!result) { throw std::bad_alloc(); }
    if (LitePerf::allocations && LitePerf::measuring)
    {
        ++LitePerf::heapNewCount;
        LitePerf::heapNewBytes += size;
    }
    return result;
}
void* operator new[](size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}
void operator delete(void* memory, std::align_val_t) noexcept
{
    if (memory && LitePerf::allocations && LitePerf::measuring)
    {
        ++LitePerf::heapDeleteCount;
    }
    _aligned_free(memory);
}
void operator delete[](void* memory, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}
void operator delete(void* memory, size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}
void operator delete[](void* memory, size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}
