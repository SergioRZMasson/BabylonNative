#pragma once

#include <babylon_lite.h>
#include <gtest/gtest.h>
#include <cstdlib>
#if defined(_WIN32)
#include <malloc.h>
#endif

struct TestRuntime
{
    bl_Runtime* runtime{};
    bl_Status status{};

    static void* Allocate(void*, size_t bytes, size_t alignment)
    {
#if defined(_WIN32)
        return _aligned_malloc(bytes, alignment < sizeof(void*) ? sizeof(void*) : alignment);
#else
        const size_t actualAlignment = alignment < sizeof(void*) ? sizeof(void*) : alignment;
        return std::aligned_alloc(actualAlignment, (bytes + actualAlignment - 1) & ~(actualAlignment - 1));
#endif
    }

    static void Deallocate(void*, void* memory, size_t, size_t)
    {
#if defined(_WIN32)
        _aligned_free(memory);
#else
        std::free(memory);
#endif
    }

    explicit TestRuntime(const bl_AudioService* audio = nullptr, const bl_ShaderCompilerService* compiler = nullptr)
    {
        const bl_RuntimeOptions options{{nullptr, Allocate, Deallocate}, nullptr, nullptr, compiler, audio, nullptr};
        status = bl_createRuntime(&options, &runtime);
    }

    ~TestRuntime()
    {
        if (runtime)
        {
            EXPECT_EQ(bl_disposeRuntime(runtime), BL_OK);
        }
    }
};
