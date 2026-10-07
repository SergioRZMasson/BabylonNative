#pragma once
#include <stdint.h>

extern "C" int64_t lite_perf_tick();
extern "C" void lite_perf_add(unsigned phase, int64_t start);
extern "C" void lite_perf_count(unsigned count, uint64_t amount);
extern "C" bool lite_perf_grouping_cache();
extern "C" void lite_perf_guard(const void* bytes, size_t size);
