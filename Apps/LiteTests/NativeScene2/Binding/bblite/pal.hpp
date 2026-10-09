#pragma once
#include "runtime.hpp"
#include <chrono>

namespace bbl::pal
{
    inline double performance_milliseconds()
    {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}
