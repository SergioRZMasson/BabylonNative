#pragma once

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>

// The scalar test-application profile needs only error values and an empty
// closure trace visitor. It is not bblitec's runtime or a Babylon engine.
namespace bbl::js
{
    struct TraceVisitor
    {
    };

    inline std::exception_ptr make_error(const std::string& kind, const std::string& message,
        std::exception_ptr)
    {
        return std::make_exception_ptr(std::runtime_error(kind + ": " + message));
    }
}
